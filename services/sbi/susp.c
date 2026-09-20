// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI system suspend extension (EID "SUSP").
 *
 * Suspend to RAM without platform support: with every other hart stopped,
 * the calling hart waits in M-mode for an interrupt S-mode has enabled and
 * resumes at resume_addr the way a non-retentive hart suspend does. Memory
 * and devices keep their state, which is all the caller may rely on.
 */

#include <arch/hsm.h>
#include <util.h>

#include "sbi_internal.h"

static struct service_ret sbi_susp_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	if (fid != SBI_SUSP_SYSTEM_SUSPEND)
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	/* sleep_type is 32 bits wide; 0x80000000 and up are platform types. */
	if (regs->a0 > UL(0xffffffff))
		return sbi_err(SBI_ERR_INVALID_PARAM);
	if (regs->a0 != SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM)
		return sbi_err(regs->a0 >= UL(0x80000000) ?
			       SBI_ERR_NOT_SUPPORTED :
			       SBI_ERR_INVALID_PARAM);
	return sbi_err(hsm_system_suspend(regs->a1, regs->a2));
}

SERVICE_DEFINE(sbi_susp) = {
	.name = "sbi-susp",
	.eid_min = SBI_EXT_SUSP,
	.eid_max = SBI_EXT_SUSP,
	.ecall = sbi_susp_ecall,
};
