// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * CPPC through the platform microcontroller: RPMI CPPC service group. RPMI
 * and SBI number the CPPC registers alike.
 *
 * A PuC can offer fast channels, shared memory a performance request is
 * just written to: 8 bytes per hart for requests (the desired performance,
 * or in autonomous mode the minimum and maximum), 8 for feedback, and
 * maybe a doorbell register to say that something was written. Where they
 * are is asked once, at boot if the PuC answers by then: the region is the
 * monitor's alone in that case, fenced off and reserved like its other
 * memory. A PuC that shows up later still gets its fast channels used, but
 * by then there is no taking the memory away from S-mode. Requests that do
 * not fit a fast channel (wider than 32 bits), and everything else, go by
 * message.
 */

#include <arch/hart.h>
#include <arch/pmp.h>
#include <atomic.h>
#include <cppc.h>
#include <driver.h>
#include <io.h>
#include <log.h>
#include <memregion.h>
#include <rpmi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <types_ext.h>
#include <util.h>

/* The PuC behind the node's "mboxes"; NULL until a node was probed. */
static struct rpmi_context *puc;

static long rpmi_to_sbi(int rc)
{
	switch (rc) {
	case RPMI_SUCCESS:
		return SBI_SUCCESS;
	case RPMI_ERR_INVALID_PARAM:
		return SBI_ERR_INVALID_PARAM;
	case RPMI_ERR_NOT_SUPPORTED:
		return SBI_ERR_NOT_SUPPORTED;
	case RPMI_ERR_DENIED:
		return SBI_ERR_DENIED;
	default:
		return SBI_ERR_FAILED;
	}
}

/* ---- fast channels ------------------------------------------------------ */

enum { FC_UNKNOWN, FC_NONE, FC_READY };

static struct {
	unsigned long state; /* atomic, FC_* */
	bool autonomous;
	unsigned int db_bytes; /* 0: no doorbell */
	unsigned long base, size, db_addr;
	uint32_t db_value;
} fc;

/* By hart index: the addresses of the hart's two channels, once asked for. */
static struct {
	bool known, usable;
	unsigned long request, feedback;
} fc_harts[CONFIG_PLATFORM_HART_COUNT];

static unsigned long fc_lock = SPINLOCK_UNLOCK;

/* An address of 64 bits from two words; false when this hart cannot form it. */
static bool addr_from(uint32_t lo, uint32_t hi, unsigned long *addr)
{
	*addr = (unsigned long)(reg_pair_to_64(hi, lo));
	return sizeof(long) == 8 || !hi;
}

/*
 * Ask the PuC, once it answers. 'boot': on the boot hart with the memory
 * regions still open, so that the region can become one. Either way it has
 * to be memory the monitor may be pointed at: not what is its own already.
 */
static void fc_discover(bool boot)
{
	uint32_t resp[9] = { 0 };
	unsigned long base = 0, size = 0, db = 0;
	unsigned int db_bytes = 0;
	int rc = 0;

	spin_lock(&fc_lock);
	if (atomic_load_ulong(&fc.state) != FC_UNKNOWN)
		goto out;
	rc = rpmi_call(puc, RPMI_GROUP_CPPC, RPMI_CPPC_GET_FAST_CHANNEL_REGION,
		       NULL, 0, resp, 9);
	/* Nobody there yet: ask again next time. */
	if (rc == RPMI_ERR_TIMEOUT || rc == RPMI_ERR_IO || rc == RPMI_ERR_BUSY)
		goto out;
	atomic_store_ulong(&fc.state, FC_NONE);
	if (rc)
		goto out;

	if (resp[1] & RPMI_CPPC_FC_DOORBELL)
		db_bytes = BIT32(RPMI_CPPC_FC_DB_WIDTH(resp[1]));
	if (!addr_from(resp[2], resp[3], &base) ||
	    !addr_from(resp[4], resp[5], &size) || !IS_POWER_OF_TWO(size) ||
	    !IS_ALIGNED(base, 8) || base + size < base ||
	    RPMI_CPPC_FC_MODE(resp[1]) > 1 || db_bytes > 4 ||
	    (db_bytes && (!addr_from(resp[6], resp[7], &db) ||
			  !IS_ALIGNED(db, db_bytes))) ||
	    !monitor_range_clear(base, size) ||
	    (db_bytes && !monitor_range_clear(db, db_bytes))) {
		pr_warn("rpmi-cppc: fast channels at %lx+%lx, doorbell %lx: not usable\n",
			base, size, db);
		goto out;
	}
	if (boot)
		memregion_add(base, size, MEMREGION_MMODE_RW);

	fc.autonomous = RPMI_CPPC_FC_MODE(resp[1]) == 1;
	fc.base = base;
	fc.size = size;
	fc.db_bytes = db_bytes;
	fc.db_addr = db;
	fc.db_value = resp[8];
	atomic_store_ulong(&fc.state, FC_READY);
	pr_info("rpmi-cppc: fast channels at %lx+%lx%s%s%s\n", base, size,
		fc.autonomous ? ", autonomous mode" : "",
		db_bytes ? ", doorbell" : "",
		boot ? "" : " (not protected: the PuC was not there at boot)");
out:
	spin_unlock(&fc_lock);
}

/* The calling hart's fast channels; false when it has none. */
static bool fc_hart(unsigned long *request, unsigned long *feedback)
{
	unsigned int self = this_hart_index();
	uint32_t req = (uint32_t)this_hartid(), resp[5] = { 0 };
	unsigned long off[2] = { 0, 0 };

	if (atomic_load_ulong(&fc.state) == FC_UNKNOWN)
		fc_discover(false);
	if (atomic_load_ulong(&fc.state) != FC_READY)
		return false;
	/* Only this hart asks about this hart. */
	if (!fc_harts[self].known) {
		int rc = rpmi_call(puc, RPMI_GROUP_CPPC,
				   RPMI_CPPC_GET_FAST_CHANNEL_OFFSET, &req, 1,
				   resp, 5);

		if (rc == RPMI_ERR_TIMEOUT || rc == RPMI_ERR_IO ||
		    rc == RPMI_ERR_BUSY)
			return false;
		fc_harts[self].usable =
			!rc && addr_from(resp[1], resp[2], &off[0]) &&
			addr_from(resp[3], resp[4], &off[1]) &&
			IS_ALIGNED(off[0] | off[1], 8) && off[0] != off[1] &&
			off[0] <= fc.size - 8 && off[1] <= fc.size - 8;
		fc_harts[self].request = fc.base + off[0];
		fc_harts[self].feedback = fc.base + off[1];
		fc_harts[self].known = true;
	}
	*request = fc_harts[self].request;
	*feedback = fc_harts[self].feedback;
	return fc_harts[self].usable;
}

static void fc_ring(void)
{
	vaddr_t db = 0;

	if (!fc.db_bytes)
		return;
	db = (vaddr_t)smode_access_begin(fc.db_addr, fc.db_bytes);
	if (fc.db_bytes == 1)
		io_write8(db, (uint8_t)fc.db_value);
	else if (fc.db_bytes == 2)
		io_write16(db, (uint16_t)fc.db_value);
	else
		io_write32(db, fc.db_value);
	smode_access_end();
}

/* true: the write was one for the request fast channel, and is done. */
static bool fc_write(uint32_t reg, uint64_t val)
{
	unsigned long request = 0, feedback = 0;
	vaddr_t chan = 0;
	unsigned int word = 0;

	if (fc.autonomous ?
	    reg != CPPC_REG_MIN_PERF && reg != CPPC_REG_MAX_PERF :
	    reg != CPPC_REG_DESIRED_PERF)
		return false;
	if (val > ULL(0xffffffff) || !fc_hart(&request, &feedback))
		return false;

	/* (desired, 0), or (minimum, maximum). */
	word = reg == CPPC_REG_MAX_PERF;
	chan = (vaddr_t)smode_access_begin(request, 8);
	io_write32(chan + 4 * word, (uint32_t)val);
	if (!fc.autonomous)
		io_write32(chan + 4, 0);
	smode_access_end();
	fc_ring();
	return true;
}

#ifdef CONFIG_CPPC_RPMI_FEEDBACK_AS_DELIVERED_CTR
static bool fc_read(uint32_t reg, uint64_t *val)
{
	unsigned long request = 0, feedback = 0;
	vaddr_t chan = 0;
	uint32_t hi = 0;

	if (reg != CPPC_REG_DELIVERED_CTR || !fc_hart(&request, &feedback))
		return false;
	/* The PuC writes it whenever it likes: until the halves agree. */
	chan = (vaddr_t)smode_access_begin(feedback, 8);
	do {
		hi = io_read32(chan + 4);
		*val = reg_pair_to_64(hi, io_read32(chan));
	} while (hi != io_read32(chan + 4));
	smode_access_end();
	return true;
}
#else
static bool fc_read(uint32_t reg, uint64_t *val)
{
	return false;
}
#endif

/* ---- the backend ------------------------------------------------------- */

static long rpmi_cppc_probe(uint32_t reg, uint32_t *width)
{
	uint32_t req[2] = { reg, (uint32_t)this_hartid() }, resp[2] = { 0 };
	int rc = rpmi_call(puc, RPMI_GROUP_CPPC, RPMI_CPPC_PROBE_REG, req, 2,
			   resp, 2);

	/* "Not there" is an answer to a probe, not a failure. */
	*width = rc ? 0 : resp[1];
	return rc == RPMI_ERR_NOT_SUPPORTED ? SBI_SUCCESS : rpmi_to_sbi(rc);
}

static long rpmi_cppc_read(uint32_t reg, uint64_t *val)
{
	uint32_t req[2] = { reg, (uint32_t)this_hartid() }, resp[3] = { 0 };
	int rc = 0;

	if (fc_read(reg, val))
		return SBI_SUCCESS;
	rc = rpmi_call(puc, RPMI_GROUP_CPPC, RPMI_CPPC_READ_REG, req, 2, resp,
		       3);
	*val = reg_pair_to_64(resp[2], resp[1]);
	return rpmi_to_sbi(rc);
}

static long rpmi_cppc_write(uint32_t reg, uint64_t val)
{
	uint32_t req[4] = { reg, (uint32_t)this_hartid(), (uint32_t)val,
			    high32_from_64(val) };
	uint32_t resp[1] = {};

	if (fc_write(reg, val))
		return SBI_SUCCESS;
	return rpmi_to_sbi(rpmi_call(puc, RPMI_GROUP_CPPC, RPMI_CPPC_WRITE_REG,
				     req, 4, resp, 1));
}

static const struct cppc_ops rpmi_cppc_ops = {
	.name = "rpmi",
	.probe = rpmi_cppc_probe,
	.read = rpmi_cppc_read,
	.write = rpmi_cppc_write,
};

static int rpmi_cppc_drv_probe(const void *fdt, int node)
{
	uint16_t group = 0;

	/*
	 * "mboxes = <&transport group>": which PuC, and a check on the group.
	 */
	if (node < 0 || puc)
		return 0;
	if (rpmi_client_from_fdt(fdt, node, &puc, &group) ||
	    group != RPMI_GROUP_CPPC) {
		puc = NULL;
		return -1;
	}
	cppc_register(&rpmi_cppc_ops);
	fc_discover(true);
	return 0;
}

static const char *const rpmi_cppc_drv_compatible[] = { "riscv,rpmi-cppc",
							NULL };

DRIVER_DEFINE(rpmi_cppc_drv) = {
	.name = "rpmi-cppc",
	.compatible = rpmi_cppc_drv_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = rpmi_cppc_drv_probe,
};
