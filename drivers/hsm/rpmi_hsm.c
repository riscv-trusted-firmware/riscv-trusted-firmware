// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Hart power through the platform microcontroller: RPMI HSM service group.
 * The PuC releases a hart into the monitor's entry point, where it finds
 * its way to the pending start like a hart that was only waiting, and may
 * power a hart down once it has been told the hart stops and sees it in
 * WFI.
 *
 * A PuC that does not answer is taken for one that does not manage hart
 * power: harts then simply wait in WFI, which works everywhere. An answer
 * that says no is an error.
 */

#include <arch/hsm.h>
#include <driver.h>
#include <log.h>
#include <rpmi.h>
#include <sbi/sbi.h>
#include <util.h>

/* The PuC behind the node's "mboxes"; NULL until a node was probed. */
static struct rpmi_context *puc;

/* Nobody there, or nobody who does hart power (no transport, no HSM group). */
static bool silent(int rc)
{
	return rc == RPMI_ERR_TIMEOUT || rc == RPMI_ERR_IO ||
	       rc == RPMI_ERR_NOT_SUPPORTED;
}

static long rpmi_hsm_hart_start(unsigned long hartid)
{
	uint64_t entry = CONFIG_MONITOR_LOAD_ADDR;
	uint32_t req[3] = { (uint32_t)hartid, (uint32_t)entry,
			    high32_from_64(entry) };
	uint32_t resp[1] = {};
	int rc = rpmi_call(puc, RPMI_GROUP_HSM, RPMI_HSM_HART_START, req, 3,
			   resp, 1);

	/* A hart the PuC finds running already is what we want, too. */
	if (!rc || rc == RPMI_ERR_ALREADY || silent(rc))
		return SBI_SUCCESS;
	pr_warn("rpmi-hsm: hart %lu start refused (%d)\n", hartid, rc);
	return SBI_ERR_FAILED;
}

static void rpmi_hsm_hart_stop(unsigned long hartid)
{
	uint32_t req = (uint32_t)hartid, resp[1] = {};
	int rc = rpmi_call(puc, RPMI_GROUP_HSM, RPMI_HSM_HART_STOP, &req, 1,
			   resp, 1);

	if (rc && !silent(rc))
		pr_warn("rpmi-hsm: hart %lu stop refused (%d)\n", hartid, rc);
}

static const struct hsm_ops rpmi_hsm_ops = {
	.name = "rpmi",
	.hart_start = rpmi_hsm_hart_start,
	.hart_stop = rpmi_hsm_hart_stop,
};

static int rpmi_hsm_probe(const void *fdt, int node)
{
	uint16_t group = 0;

	/*
	 * "mboxes = <&transport group>": which PuC, and a check on the group.
	 */
	if (node < 0 || puc)
		return 0;
	if (rpmi_client_from_fdt(fdt, node, &puc, &group) ||
	    group != RPMI_GROUP_HSM) {
		puc = NULL;
		return -1;
	}
	hsm_register(&rpmi_hsm_ops);
	return 0;
}

static const char *const rpmi_hsm_compatible[] = { "riscv,rpmi-hsm", NULL };

DRIVER_DEFINE(rpmi_hsm) = {
	.name = "rpmi-hsm",
	.compatible = rpmi_hsm_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = rpmi_hsm_probe,
};
