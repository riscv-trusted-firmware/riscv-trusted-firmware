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
 *
 * Suspend: the PuC's suspend types are SBI HSM suspend types, the platform
 * specific ones being what it adds to the SBI's two. The list is asked for
 * when a hart first suspends, not at boot, when the PuC may not be up.
 */

#include <arch/hart.h>
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
	uint64_t entry = monitor_base();
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

#define MAX_SUSPEND_TYPES CONFIG_HSM_RPMI_MAX_SUSPEND_TYPES

static uint32_t suspend_types[MAX_SUSPEND_TYPES];
static unsigned int nr_suspend_types;
static bool suspend_types_known;

/* (START_INDEX) -> (STATUS, REMAINING, RETURNED, SUSPEND_TYPE[RETURNED]) */
static int suspend_types_fetch(void)
{
	uint32_t resp[3 + MAX_SUSPEND_TYPES] = {}, start = 0, remaining = 0;
	unsigned int n = 0;
	size_t len = 0;
	int rc = 0;

	do {
		rc = rpmi_request(puc, RPMI_GROUP_HSM,
				  RPMI_HSM_GET_SUSPEND_TYPES, &start,
				  sizeof(start), resp, sizeof(resp), &len);
		if (rc)
			return rc;
		if (len < 4 || (int32_t)resp[0])
			return len < 4 ? RPMI_ERR_IO : (int32_t)resp[0];
		if (len < 12 || len < 12 + 4 * (size_t)resp[2] ||
		    (!resp[2] && resp[1]))
			return RPMI_ERR_IO;
		remaining = resp[1];
		for (uint32_t i = 0; i < resp[2] && n < MAX_SUSPEND_TYPES; i++)
			suspend_types[n++] = resp[3 + i];
		start += resp[2];
	} while (remaining && n < MAX_SUSPEND_TYPES);

	/* Harts racing here have asked the same PuC the same question. */
	nr_suspend_types = n;
	suspend_types_known = true;
	return 0;
}

static long rpmi_hsm_hart_suspend(unsigned long hartid, uint32_t type,
				  unsigned long resume_addr)
{
	uint32_t req[4] = { (uint32_t)hartid, type, (uint32_t)resume_addr,
			    (uint32_t)((uint64_t)resume_addr >> 32) };
	uint32_t resp[1] = {};
	int rc = 0;

	/*
	 * The SBI's own two types need nobody: the PuC hears of them if its
	 * list is there and has them, and a hart that idles does not go asking
	 * for the list. A platform type is the PuC's by definition.
	 */
	if (!suspend_types_known && (type == SBI_HSM_SUSPEND_RET_DEFAULT ||
				     type == SBI_HSM_SUSPEND_NON_RET_DEFAULT))
		return SBI_ERR_NOT_SUPPORTED;
	rc = suspend_types_known ? 0 : suspend_types_fetch();
	if (rc)
		return silent(rc) ? SBI_ERR_NOT_SUPPORTED : SBI_ERR_FAILED;
	for (unsigned int i = 0;; i++) {
		if (i == nr_suspend_types)
			return SBI_ERR_NOT_SUPPORTED;
		if (suspend_types[i] == type)
			break;
	}

	rc = rpmi_call(puc, RPMI_GROUP_HSM, RPMI_HSM_HART_SUSPEND, req, 4, resp,
		       1);
	if (!rc)
		return SBI_SUCCESS;
	if (!silent(rc))
		pr_warn("rpmi-hsm: hart %lu suspend type %x refused (%d)\n",
			hartid, type, rc);
	return rc == RPMI_ERR_INVALID_PARAM ? SBI_ERR_INVALID_PARAM :
					      SBI_ERR_FAILED;
}

static const struct hsm_ops rpmi_hsm_ops = {
	.name = "rpmi",
	.hart_start = rpmi_hsm_hart_start,
	.hart_stop = rpmi_hsm_hart_stop,
	.hart_suspend = rpmi_hsm_hart_suspend,
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
