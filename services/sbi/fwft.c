// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI firmware features extension (EID "FWFT").
 */

#include <arch/fwft.h>
#include <util.h>

#include "sbi_internal.h"

static struct service_ret sbi_fwft_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	unsigned long value = 0;
	long rc = 0;

	switch (fid) {
	case SBI_FWFT_SET:
		/* feature is 32 bits wide on every XLEN. */
		if (regs->a0 > UL(0xffffffff))
			return sbi_err(SBI_ERR_DENIED);
		return sbi_err(fwft_set(regs->a0, regs->a1, regs->a2));
	case SBI_FWFT_GET:
		if (regs->a0 > UL(0xffffffff))
			return sbi_err(SBI_ERR_DENIED);
		rc = fwft_get(regs->a0, &value);
		return sbi_ret(rc, (long)value);
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_fwft) = {
	.name = "sbi-fwft",
	.eid_min = SBI_EXT_FWFT,
	.eid_max = SBI_EXT_FWFT,
	.ecall = sbi_fwft_ecall,
};
