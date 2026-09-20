// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * QEMU virt machine. QEMU describes the machine in the device tree it
 * passes; there is nothing board-specific left to do beyond choosing the
 * console driver.
 */

#include <drivers/serial/uart8250.h>
#include <fdt_util.h>
#include <platform.h>
#include <util.h>

void plat_early_init(const void *fdt)
{
	uart8250_console_init(fdt);
}

void plat_init(void)
{
}

#if defined(IMAGE_MONITOR) && defined(CONFIG_QEMU_VIRT_RPMI)

#include <rpmi.h>

/*
 * QEMU virt has no platform microcontroller, and says nothing about one.
 * For the SBI test payload's PuC model, describe an RPMI shared memory
 * transport in RAM, MPXY channels for the clock and voltage groups (the
 * latter is there to be probed and found missing) and the M-mode users of
 * the transport, the way a platform with a PuC would.
 */
#define SHMEM_BASE CONFIG_QEMU_VIRT_RPMI_SHMEM_BASE
#define QUEUE_SIZE CONFIG_QEMU_VIRT_RPMI_QUEUE_SIZE

static int add_rpmi_user(void *fdt, int parent, const char *name,
			 const char *compatible, uint32_t transport,
			 uint32_t group, uint32_t channel)
{
	int node = fdt_add_subnode(fdt, parent, name), rc = 0;
	fdt32_t mboxes[2] = { cpu_to_fdt32(transport), cpu_to_fdt32(group) };

	if (node < 0)
		return node;
	rc = fdt_setprop(fdt, node, "compatible", compatible,
			 (int)strlen(compatible) + 1);
	if (!rc)
		rc = fdt_setprop(fdt, node, "mboxes", mboxes, sizeof(mboxes));
	if (!rc && channel)
		rc = fdt_setprop_u32(fdt, node, "riscv,sbi-mpxy-channel-id",
				     channel);
	return rc;
}

static int rpmi_fdt_prepare(void *fdt)
{
	static const char mbox_compatible[] = "riscv,rpmi-shmem-mbox";
	static const char names[] = "a2p-req\0p2a-ack\0p2a-req\0a2p-ack";
	const uint32_t channel = CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE;
	int soc = fdt_path_offset(fdt, "/soc"), node = 0, ac = 0, sc = 0,
	    rc = 0;
	fdt32_t reg[4 * 4] = {}, *p = reg;
	uint32_t phandle = 0;

	if (soc < 0)
		return soc;
	ac = fdt_address_cells(fdt, soc);
	sc = fdt_size_cells(fdt, soc);
	if (ac < 1 || ac > 2 || sc < 1 || sc > 2)
		return -FDT_ERR_BADNCELLS;
	rc = fdt_generate_phandle(fdt, &phandle);
	if (rc)
		return rc;

	for (unsigned int q = 0; q < RPMI_QUEUE_COUNT; q++) {
		uint64_t base = SHMEM_BASE + (uint64_t)q * QUEUE_SIZE;

		if (ac == 2)
			*p++ = cpu_to_fdt32(high32_from_64(base));
		*p++ = cpu_to_fdt32((uint32_t)base);
		if (sc == 2)
			*p++ = cpu_to_fdt32(0);
		*p++ = cpu_to_fdt32(QUEUE_SIZE);
	}

	node = fdt_add_subnode(fdt, soc, "rpmi-mailbox");
	if (node < 0)
		return node;
	rc = fdt_setprop(fdt, node, "compatible", mbox_compatible,
			 sizeof(mbox_compatible));
	if (!rc)
		rc = fdt_setprop(fdt, node, "reg", reg,
				 (int)((char *)p - (char *)reg));
	if (!rc)
		rc = fdt_setprop(fdt, node, "reg-names", names, sizeof(names));
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "riscv,slot-size",
				     CONFIG_QEMU_VIRT_RPMI_SLOT_SIZE);
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "#mbox-cells", 1);
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "phandle", phandle);

		/*
		 * Offsets move as nodes are added: look /soc up again each
		 * time.
		 */
#define USER(name, compatible, group, chan)                                   \
	do {                                                                  \
		if (!rc)                                                      \
			rc = add_rpmi_user(fdt, fdt_path_offset(fdt, "/soc"), \
					   name, compatible, phandle, group,  \
					   chan);                             \
	} while (0)
	USER("rpmi-clock", "riscv,rpmi-mpxy-clock", RPMI_GROUP_CLOCK, channel);
	USER("rpmi-voltage", "riscv,rpmi-mpxy-voltage", RPMI_GROUP_VOLTAGE,
	     channel + 1);
	USER("rpmi-system-reset", "riscv,rpmi-system-reset",
	     RPMI_GROUP_SYSTEM_RESET, 0);
	USER("rpmi-system-suspend", "riscv,rpmi-system-suspend",
	     RPMI_GROUP_SYSTEM_SUSPEND, 0);
	USER("rpmi-hsm", "riscv,rpmi-hsm", RPMI_GROUP_HSM, 0);
	USER("rpmi-cppc", "riscv,rpmi-cppc", RPMI_GROUP_CPPC, 0);
#undef USER
	return rc;
}

#else

static int rpmi_fdt_prepare(void *fdt)
{
	return 0;
}

#endif

#if defined(IMAGE_MONITOR) && defined(CONFIG_QEMU_VIRT_DOMAINS)

#include <arch/hart.h>

/* libfdt's set_string() does not survive -Wconversion. */
static int set_string(void *fdt, int node, const char *name, const char *val)
{
	return fdt_setprop(fdt, node, name, val, (int)strlen(val) + 1);
}

/*
 * Domains for the SBI test payload to meet, all of them running parts of
 * that one image (entry points 4 bytes apart at its start):
 *
 *   trusted    no hart of its own: entered from "untrusted". The image to
 *              run from, memory of its own, a page shared with the others.
 *   untrusted  the test payload proper: everything but the memory of the
 *              other two, every hart but the island's.
 *   island     the last hart, which it boots on, and memory of its own.
 */
#define DOM_IMAGE CONFIG_SBITEST_LOAD_ADDR
#define DOM_MEM CONFIG_QEMU_VIRT_DOMAINS_MEM
#define DOM_MEM_ORDER 20

static int add_memregion(void *fdt, const char *name, uint64_t base,
			 uint32_t order, uint32_t *phandle)
{
	int config = fdt_path_offset(fdt, "/chosen/riscv-domains"), node = 0,
	    rc = 0;
	fdt32_t cells[2] = { cpu_to_fdt32(high32_from_64(base)),
			     cpu_to_fdt32((uint32_t)base) };

	node = fdt_add_subnode(fdt, config, name);
	if (node < 0)
		return node;
	rc = fdt_generate_phandle(fdt, phandle);
	if (!rc)
		rc = set_string(fdt, node, "compatible",
				"riscv,domain,memregion");
	if (!rc)
		rc = fdt_setprop(fdt, node, "base", cells, sizeof(cells));
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "order", order);
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "phandle", *phandle);
	return rc;
}

static int add_instance(void *fdt, const char *name, const fdt32_t *harts,
			int nr_harts, const fdt32_t *regions, int nr_regions,
			uint64_t next_addr, bool all, uint32_t *phandle)
{
	int config = fdt_path_offset(fdt, "/chosen/riscv-domains"), node = 0,
	    rc = 0;
	fdt32_t cells[2] = { cpu_to_fdt32(high32_from_64(next_addr)),
			     cpu_to_fdt32((uint32_t)next_addr) };

	node = fdt_add_subnode(fdt, config, name);
	if (node < 0)
		return node;
	rc = fdt_generate_phandle(fdt, phandle);
	if (!rc)
		rc = set_string(fdt, node, "compatible",
				"riscv,domain,instance");
	if (!rc)
		rc = fdt_setprop(fdt, node, "possible-harts", harts,
				 nr_harts * 4);
	if (!rc)
		rc = fdt_setprop(fdt, node, "regions", regions, nr_regions * 8);
	if (!rc && next_addr)
		rc = fdt_setprop(fdt, node, "next-addr", cells, sizeof(cells));
	if (!rc && !all)
		rc = fdt_setprop(fdt, node, "boot-hart", &harts[nr_harts - 1],
				 4);
	if (!rc && all)
		rc = set_string(fdt, node, "root-regions-inheritance", "all");
	if (!rc && all)
		rc = fdt_setprop_empty(fdt, node, "system-reset-allowed");
	if (!rc && all)
		rc = fdt_setprop_empty(fdt, node, "system-suspend-allowed");
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "phandle", *phandle);
	return rc;
}

/* The phandle of the cpu node of 'hartid', given one when it has none. */
static int cpu_phandle(void *fdt, unsigned long hartid, uint32_t *phandle)
{
	int cpus = fdt_path_offset(fdt, "/cpus"), cpu = 0, rc = 0;
	uint64_t id = 0;

	fdt_for_each_subnode(cpu, fdt, cpus)
		if (!fdt_reg(fdt, cpu, 0, &id, NULL) && id == hartid)
			break;
	if (cpu < 0)
		return cpu;
	*phandle = fdt_get_phandle(fdt, cpu);
	if (*phandle)
		return 0;
	rc = fdt_generate_phandle(fdt, phandle);
	return rc ? rc : fdt_setprop_u32(fdt, cpu, "phandle", *phandle);
}

static int assign_cpu(void *fdt, unsigned long hartid, uint32_t domain)
{
	int cpus = fdt_path_offset(fdt, "/cpus"), cpu = 0;
	uint64_t id = 0;

	fdt_for_each_subnode(cpu, fdt, cpus)
		if (!fdt_reg(fdt, cpu, 0, &id, NULL) && id == hartid)
			return fdt_setprop_u32(fdt, cpu, "riscv,domain",
					       domain);
	return -FDT_ERR_NOTFOUND;
}

static int domains_fdt_prepare(void *fdt)
{
	enum { IMAGE, TMEM, IMEM, SHARED, NR_MEM };
	uint32_t mem[NR_MEM], cpus[CONFIG_PLATFORM_HART_COUNT], dom[3];
	fdt32_t harts[CONFIG_PLATFORM_HART_COUNT + 1], regions[2 * NR_MEM];
	unsigned int n = 0, island;
	uint32_t image_order = (uint32_t)__builtin_ctzl(CONFIG_SBITEST_SIZE);
	int chosen = fdt_path_offset(fdt, "/chosen"), node, rc;

	if (chosen < 0)
		chosen = fdt_add_subnode(fdt, 0, "chosen");
	node = chosen < 0 ? chosen :
			    fdt_add_subnode(fdt, chosen, "riscv-domains");
	if (node < 0)
		return node;
	rc = set_string(fdt, node, "compatible", "riscv,domain,config");

	if (!rc)
		rc = add_memregion(fdt, "image", DOM_IMAGE, image_order,
				   &mem[IMAGE]);
	if (!rc)
		rc = add_memregion(fdt, "tmem", DOM_MEM, DOM_MEM_ORDER,
				   &mem[TMEM]);
	if (!rc)
		rc = add_memregion(fdt, "imem", DOM_MEM + BIT(DOM_MEM_ORDER),
				   DOM_MEM_ORDER, &mem[IMEM]);
	if (!rc)
		rc = add_memregion(fdt, "shared",
				   DOM_MEM + SHIFT_UL(2, DOM_MEM_ORDER), 12,
				   &mem[SHARED]);

	/*
	 * The boot hart first, the island's last: the one hart index 0 is not.
	 */
	for (unsigned int i = 0; !rc && hart_by_index(i); i++) {
		rc = cpu_phandle(fdt, hart_id_of(i), &cpus[n]);
		harts[n] = cpu_to_fdt32(cpus[n]);
		n++;
	}
	if (rc)
		return rc;
	island = n > 2 ? n - 1 : 0;

#define REGION(i, m, perm) region_set(regions, i, mem[m], perm)
	/*
	 * A new node goes in front of its siblings: the last one added is
	 * domain 1.
	 */
	REGION(0, IMAGE, 0x28);
	REGION(1, IMEM, 0x38);
	REGION(2, SHARED, 0x18);
	if (island)
		rc = add_instance(fdt, "island", &harts[island], 1, regions, 3,
				  DOM_IMAGE + 8, false, &dom[2]);

	REGION(0, TMEM, 0);
	REGION(1, IMEM, 0);
	if (!rc)
		rc = add_instance(fdt, "untrusted", harts,
				  (int)(island ? island : n), regions, 2, 0,
				  true, &dom[1]);

	REGION(0, IMAGE, 0x28);
	REGION(1, TMEM, 0x38);
	REGION(2, SHARED, 0x18);
	/* Any hart, but it boots on the boot hart: the last of this list. */
	harts[n] = harts[0];
	if (!rc)
		rc = add_instance(fdt, "trusted", &harts[1], (int)n, regions, 3,
				  DOM_IMAGE + 4, false, &dom[0]);
#undef REGION

	for (unsigned int i = 0; !rc && i < n; i++)
		rc = assign_cpu(fdt, hart_id_of(i),
				dom[island && i == island ? 2 : 1]);
	return rc;
}

#else

static int domains_fdt_prepare(void *fdt)
{
	return 0;
}

#endif

int plat_fdt_prepare(void *fdt)
{
	int rc = rpmi_fdt_prepare(fdt);

	return rc ? rc : domains_fdt_prepare(fdt);
}
