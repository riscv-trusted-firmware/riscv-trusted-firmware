// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI IPI extension (EID "sPI").
 */

#include <ipi.h>

#include "sbi_internal.h"

static long sbi_ipi_probe(unsigned long eid)
{
	return ipi_available();
}

static struct service_ret sbi_ipi_ecall(unsigned long eid, unsigned long fid,
					struct trap_regs *regs)
{
	struct hartmask targets = {};
	long rc = 0;

	if (fid != SBI_IPI_SEND_IPI || !ipi_available())
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	rc = sbi_hartmask(regs->a0, regs->a1, &targets);
	if (rc)
		return sbi_err(rc);
	ipi_send_mask(&targets, IPI_EVENT_SMODE);
	return sbi_ok(0);
}

SERVICE_DEFINE(sbi_ipi) = {
	.name = "sbi-ipi",
	.eid_min = SBI_EXT_IPI,
	.eid_max = SBI_EXT_IPI,
	.probe = sbi_ipi_probe,
	.ecall = sbi_ipi_ecall,
};
