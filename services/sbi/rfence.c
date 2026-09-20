// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI remote fence extension (EID "RFNC").
 */

#include <arch/rfence.h>
#include <ipi.h>
#include <util.h>

#include "sbi_internal.h"

static const enum rfence_type fid_to_type[] = {
	[SBI_RFENCE_FENCE_I] = RFENCE_FENCE_I,
	[SBI_RFENCE_SFENCE_VMA] = RFENCE_SFENCE_VMA,
	[SBI_RFENCE_SFENCE_VMA_ASID] = RFENCE_SFENCE_VMA_ASID,
	[SBI_RFENCE_HFENCE_GVMA_VMID] = RFENCE_HFENCE_GVMA_VMID,
	[SBI_RFENCE_HFENCE_GVMA] = RFENCE_HFENCE_GVMA,
	[SBI_RFENCE_HFENCE_VVMA_ASID] = RFENCE_HFENCE_VVMA_ASID,
	[SBI_RFENCE_HFENCE_VVMA] = RFENCE_HFENCE_VVMA,
};

static long sbi_rfence_probe(unsigned long eid)
{
	return ipi_available();
}

static struct service_ret sbi_rfence_ecall(unsigned long eid, unsigned long fid,
					   struct trap_regs *regs)
{
	struct rfence_req req = {};
	struct hartmask targets = {};
	long rc = 0;

	if (fid >= ARRAY_SIZE(fid_to_type) || !ipi_available())
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	req = (struct rfence_req){
		.type = fid_to_type[fid],
		.start = regs->a2,
		.size = regs->a3,
		.asid = regs->a4,
	};
	/* A range that wraps around the address space is not one. */
	if (req.type != RFENCE_FENCE_I && req.size != ~UL(0) &&
	    req.start + req.size < req.start)
		return sbi_err(SBI_ERR_INVALID_ADDRESS);

	rc = sbi_hartmask(regs->a0, regs->a1, &targets);
	if (rc)
		return sbi_err(rc);
	return sbi_err(rfence_request(&targets, &req));
}

SERVICE_DEFINE(sbi_rfence) = {
	.name = "sbi-rfence",
	.eid_min = SBI_EXT_RFENCE,
	.eid_max = SBI_EXT_RFENCE,
	.probe = sbi_rfence_probe,
	.ecall = sbi_rfence_ecall,
};
