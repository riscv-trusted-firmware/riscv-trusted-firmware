// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI timer extension (EID "TIME").
 */

#include <arch/hart.h>
#include <timer.h>
#include <util.h>

#include "sbi_internal.h"

static long sbi_time_probe(unsigned long eid)
{
	return timer_available() || hart_has(HART_FEAT_SSTC);
}

static struct service_ret sbi_time_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	uint64_t when = regs->a0;

	if (fid != SBI_TIME_SET_TIMER)
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
#if __RISCV_XLEN__ == 32
	when |= SHIFT_U64(regs->a1, 32);
#endif
	timer_smode_set(when);
	return sbi_ok(0);
}

SERVICE_DEFINE(sbi_time) = {
	.name = "sbi-time",
	.eid_min = SBI_EXT_TIME,
	.eid_max = SBI_EXT_TIME,
	.probe = sbi_time_probe,
	.ecall = sbi_time_ecall,
};
