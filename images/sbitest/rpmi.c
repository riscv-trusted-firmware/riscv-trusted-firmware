// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The SBI CPPC extension, served by the monitor's RPMI CPPC backend and
 * the PuC model behind it. Runs while another hart serves the model.
 */

#include <io.h>
#include <util.h>

#include "sbicall.h"
#include "sbitest.h"

#ifdef CONFIG_CPPC_RPMI

#define FID_PROBE 0
#define FID_READ 1
#define FID_READ_HI 2
#define FID_WRITE 3

void test_cppc(void)
{
	struct sbiret ret = {};

	printf("cppc\n");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_CPPC);
	CHECK(ret.value != 0, "CPPC not probed");

	ret = sbi_call1(SBI_EXT_CPPC, FID_PROBE, PUC_CPPC_REG_RO);
	CHECK(ret.error == 0 && ret.value == 32, "probe: error %ld width %ld",
	      ret.error, ret.value);
	ret = sbi_call1(SBI_EXT_CPPC, FID_PROBE, PUC_CPPC_REG_RW);
	CHECK(ret.error == 0 && ret.value == 64, "probe: error %ld width %ld",
	      ret.error, ret.value);
	/* A register the platform does not have: width 0, not an error. */
	ret = sbi_call1(SBI_EXT_CPPC, FID_PROBE, 7);
	CHECK(ret.error == 0 && ret.value == 0,
	      "probe of a missing register: %ld %ld", ret.error, ret.value);

	ret = sbi_call1(SBI_EXT_CPPC, FID_READ, PUC_CPPC_REG_RO);
	CHECK(ret.error == 0 && ret.value == PUC_CPPC_RO_VALUE, "read: %ld %ld",
	      ret.error, ret.value);
	CHECK_RET(sbi_call2(SBI_EXT_CPPC, FID_WRITE, PUC_CPPC_REG_RO, 1),
		  SBI_ERR_DENIED);
	CHECK_RET(sbi_call1(SBI_EXT_CPPC, FID_READ, 7), SBI_ERR_NOT_SUPPORTED);

	/* A 64-bit register: one argument on RV64, (lo, hi) on RV32. */
#if __RISCV_XLEN__ == 32
	ret = sbi_call3(SBI_EXT_CPPC, FID_WRITE, PUC_CPPC_REG_RW, 0x89abcdef,
			0x1234567);
#else
	ret = sbi_call2(SBI_EXT_CPPC, FID_WRITE, PUC_CPPC_REG_RW,
			UL(0x123456789abcdef));
#endif
	CHECK_RET(ret, SBI_SUCCESS);
	ret = sbi_call1(SBI_EXT_CPPC, FID_READ, PUC_CPPC_REG_RW);
	CHECK(ret.error == 0 && (uint32_t)ret.value == 0x89abcdef,
	      "read back %lx", ret.value);
	ret = sbi_call1(SBI_EXT_CPPC, FID_READ_HI, PUC_CPPC_REG_RW);
#if __RISCV_XLEN__ == 32
	CHECK(ret.error == 0 && ret.value == 0x1234567, "read_hi %lx",
	      ret.value);
#else
	CHECK(ret.error == 0 && ret.value == 0, "read_hi %lx on RV64",
	      ret.value);
	CHECK_RET(sbi_call1(SBI_EXT_CPPC, FID_READ, UL(0x100000000)),
		  SBI_ERR_INVALID_PARAM);
#endif
	CHECK_RET(sbi_call0(SBI_EXT_CPPC, 4), SBI_ERR_NOT_SUPPORTED);
}

#else

void test_cppc(void)
{
}

#endif
