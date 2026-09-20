// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI CPPC extension (EID "CPPC").
 */

#include <cppc.h>
#include <util.h>

#include "sbi_internal.h"

#define SBI_CPPC_PROBE 0
#define SBI_CPPC_READ 1
#define SBI_CPPC_READ_HI 2
#define SBI_CPPC_WRITE 3

static long sbi_cppc_probe_ext(unsigned long eid)
{
	return cppc_available();
}

static struct service_ret sbi_cppc_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	uint64_t val = 0;
	uint32_t width = 0;
	long rc = 0;

	/* cppc_reg_id is 32 bits wide on every XLEN. */
	if (fid <= SBI_CPPC_WRITE && regs->a0 > UL(0xffffffff))
		return sbi_err(SBI_ERR_INVALID_PARAM);

	switch (fid) {
	case SBI_CPPC_PROBE:
		rc = cppc_probe((uint32_t)regs->a0, &width);
		return sbi_ret(rc, (long)width);
	case SBI_CPPC_READ:
		rc = cppc_read((uint32_t)regs->a0, &val);
		return sbi_ret(rc, (long)val);
	case SBI_CPPC_READ_HI:
		/* The upper 32 bits on RV32; always zero on RV64. */
		rc = cppc_read((uint32_t)regs->a0, &val);
		return sbi_ret(rc, (long)(val >> 32 >> (__RISCV_XLEN__ - 32)));
	case SBI_CPPC_WRITE:
		/* val is 64 bits wide: two registers on RV32. */
		val = regs->a1;
#if __RISCV_XLEN__ == 32
		val |= SHIFT_U64(regs->a2, 32);
#endif
		return sbi_err(cppc_write((uint32_t)regs->a0, val));
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_cppc) = {
	.name = "sbi-cppc",
	.eid_min = SBI_EXT_CPPC,
	.eid_max = SBI_EXT_CPPC,
	.probe = sbi_cppc_probe_ext,
	.ecall = sbi_cppc_ecall,
};
