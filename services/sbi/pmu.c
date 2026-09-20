// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI performance monitoring unit extension (EID "PMU"). Counter
 * snapshots (shared memory) are not implemented.
 */

#include <arch/pmu.h>
#include <util.h>

#include "sbi_internal.h"

/* 64-bit arguments take two registers on RV32: (lo, hi). */
static uint64_t arg64(unsigned long lo, unsigned long hi)
{
#if __RISCV_XLEN__ == 32
	return SHIFT_U64(hi, 32) | lo;
#else
	return lo;
#endif
}

static struct service_ret sbi_pmu_ecall(unsigned long eid, unsigned long fid,
					struct trap_regs *regs)
{
	unsigned long out = 0;
	uint64_t val = 0;
	long rc = 0;

	switch (fid) {
	case SBI_PMU_NUM_COUNTERS:
		return sbi_ok(PMU_COUNTERS);
	case SBI_PMU_COUNTER_GET_INFO:
		rc = pmu_counter_info(regs->a0, &out);
		return sbi_ret(rc, (long)out);
	case SBI_PMU_COUNTER_CONFIG_MATCHING:
		rc = pmu_counter_config(regs->a0, regs->a1, regs->a2, regs->a3,
					arg64(regs->a4, regs->a5), &out);
		return sbi_ret(rc, (long)out);
	case SBI_PMU_COUNTER_START:
		return sbi_err(pmu_counter_start(regs->a0, regs->a1, regs->a2,
						 arg64(regs->a3, regs->a4)));
	case SBI_PMU_COUNTER_STOP:
		return sbi_err(pmu_counter_stop(regs->a0, regs->a1, regs->a2));
	case SBI_PMU_COUNTER_FW_READ:
		rc = pmu_counter_fw_read(regs->a0, &val);
		return sbi_ret(rc, (long)val);
	case SBI_PMU_COUNTER_FW_READ_HI:
		rc = pmu_counter_fw_read(regs->a0, &val);
		return sbi_ret(rc, (long)(val >> 32 >> (__RISCV_XLEN__ - 32)));
	default:
		/* Including SBI_PMU_SNAPSHOT_SET_SHMEM. */
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_pmu) = {
	.name = "sbi-pmu",
	.eid_min = SBI_EXT_PMU,
	.eid_max = SBI_EXT_PMU,
	.ecall = sbi_pmu_ecall,
};
