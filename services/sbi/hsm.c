// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI hart state management extension (EID "HSM").
 */

#include <arch/hsm.h>
#include <util.h>

#include "sbi_internal.h"

static struct service_ret sbi_hsm_ecall(unsigned long eid, unsigned long fid,
					struct trap_regs *regs)
{
	long state = 0;

	switch (fid) {
	case SBI_HSM_HART_START:
		return sbi_err(hsm_hart_start(regs->a0, regs->a1, regs->a2));
	case SBI_HSM_HART_STOP:
		return sbi_err(hsm_hart_stop());
	case SBI_HSM_HART_GET_STATUS:
		state = hsm_hart_state(regs->a0);
		return state < 0 ? sbi_err(state) : sbi_ok(state);
	case SBI_HSM_HART_SUSPEND:
		/* suspend_type is 32 bits wide on every XLEN. */
		if (regs->a0 > UL(0xffffffff))
			return sbi_err(SBI_ERR_INVALID_PARAM);
		return sbi_err(hsm_hart_suspend(regs->a0, regs->a1, regs->a2));
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_hsm) = {
	.name = "sbi-hsm",
	.eid_min = SBI_EXT_HSM,
	.eid_max = SBI_EXT_HSM,
	.ecall = sbi_hsm_ecall,
};
