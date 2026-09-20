// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI DBTR tests: an execution breakpoint and a load watchpoint on
 * ourselves. A trigger that fires is a breakpoint exception, which we take
 * directly (it is delegated) and step over.
 */

#include <arch/csr.h>
#include <io.h>
#include <util.h>

#include "sbicall.h"
#include "sbitest.h"

#ifdef CONFIG_SBI_DBTR

#define FID_NUM_TRIGGERS 0
#define FID_SET_SHMEM 1
#define FID_READ 2
#define FID_INSTALL 3
#define FID_UPDATE 4
#define FID_UNINSTALL 5
#define FID_ENABLE 6
#define FID_DISABLE 7

#define TDATA1_TYPE(t) SHIFT_UL(t, __RISCV_XLEN__ - 4)
#define TDATA1_DMODE BIT(__RISCV_XLEN__ - 5)
#define MC_LOAD BIT(0)
#define MC_EXECUTE BIT(2)
#define MC_S BIT(4)
#define MC_M BIT(6)
#define MC_CHAIN BIT(11)
#define STATE_MAPPED BIT(0)
#define STATE_S BIT(2)

static struct {
	unsigned long idx, tdata1, tdata2, tdata3;
} shmem[CONFIG_SBI_DBTR_MAX_TRIGGERS];

static unsigned long watched = 0x77a7c4ed;

static unsigned long hits(void (*fn)(void))
{
	WRITE_ONCE(trap_expected, true);
	WRITE_ONCE(trap_count, 0);
	fn();
	WRITE_ONCE(trap_expected, false);
	return trap_count;
}

static void read_watched(void)
{
	unsigned long val = 0;

	PROBE_INSN("lw %0, 0(%1)", : "=r"(val) : "r"(&watched) : "memory");
	/* A watchpoint fires before the access, and the handler skips it. */
	if (!trap_count)
		CHECK((uint32_t)val == 0x77a7c4ed,
		      "watched variable read as %lx", val);
}

void test_dbtr(void)
{
	unsigned long total = 0, type = 0, idx = 0;
	struct sbiret ret = {};

	printf("dbtr\n");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_DBTR);
	if (!ret.value) {
		printf("  no debug triggers on this hart\n");
		return;
	}
	ret = sbi_call1(SBI_EXT_DBTR, FID_NUM_TRIGGERS, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	total = (unsigned long)ret.value;
	CHECK(total > 0 && total <= CONFIG_SBI_DBTR_MAX_TRIGGERS,
	      "%lu triggers", total);

	/* mcontrol6 where the hart has it, the older mcontrol otherwise. */
	type = sbi_call1(SBI_EXT_DBTR, FID_NUM_TRIGGERS, TDATA1_TYPE(6)).value ?
		       6 :
		       2;
	ret = sbi_call1(SBI_EXT_DBTR, FID_NUM_TRIGGERS, TDATA1_TYPE(type));
	printf("  %lu triggers, %ld of type %lu\n", total, ret.value, type);
	CHECK(ret.value > 0, "no address match triggers");
	ret = sbi_call1(SBI_EXT_DBTR, FID_NUM_TRIGGERS, TDATA1_TYPE(15));
	CHECK(ret.value == 0, "triggers of type 15");

	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1), SBI_ERR_NO_SHMEM);
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_READ, 0, 1), SBI_ERR_NO_SHMEM);
	CHECK_RET(sbi_call3(SBI_EXT_DBTR, FID_SET_SHMEM,
			    (unsigned long)shmem + 1, 0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_DBTR, FID_SET_SHMEM, (unsigned long)shmem,
			    0, 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_DBTR, FID_SET_SHMEM, monitor_addr, 0, 0),
		  SBI_ERR_INVALID_ADDRESS);
	CHECK_RET(sbi_call3(SBI_EXT_DBTR, FID_SET_SHMEM, (unsigned long)shmem,
			    0, 0),
		  SBI_SUCCESS);

	/* What S-mode may not ask for. */
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_M | MC_EXECUTE;
	shmem[0].tdata2 = (unsigned long)dbtr_target;
	shmem[0].tdata3 = 0;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1),
		  SBI_ERR_INVALID_PARAM);
	shmem[0].tdata1 = TDATA1_TYPE(type) | TDATA1_DMODE | MC_S | MC_EXECUTE;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1),
		  SBI_ERR_INVALID_PARAM);
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_EXECUTE | MC_CHAIN;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1),
		  SBI_ERR_INVALID_PARAM);
	shmem[0].tdata1 = TDATA1_TYPE(15) | MC_S;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, total + 1),
		  SBI_ERR_BAD_RANGE);
	CHECK(hits(dbtr_target) == 0, "breakpoint without a trigger");

	/*
	 * An execution breakpoint: fires, can be turned off and on, and moved.
	 */
	shmem[0].idx = ~UL(0);
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_EXECUTE;
	ret = sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1);
	CHECK_RET(ret, SBI_SUCCESS);
	idx = shmem[0].idx;
	CHECK(idx < total, "trigger index %lx", idx);
	CHECK(hits(dbtr_target) == 1 && trap_cause == CAUSE_BREAKPOINT,
	      "%lu traps, cause %lu", trap_count, trap_cause);

	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_READ, idx, 1), SBI_SUCCESS);
	CHECK((shmem[0].idx & (STATE_MAPPED | STATE_S)) ==
	      (STATE_MAPPED | STATE_S) &&
	      shmem[0].tdata2 == (unsigned long)dbtr_target,
	      "read back: state %lx tdata2 %lx", shmem[0].idx, shmem[0].tdata2);

	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_DISABLE, idx, 1), SBI_SUCCESS);
	CHECK(hits(dbtr_target) == 0, "disabled trigger fired");
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_ENABLE, idx, 1), SBI_SUCCESS);
	CHECK(hits(dbtr_target) == 1, "re-enabled trigger did not fire");

	/* Update: the same trigger becomes a load watchpoint. */
	shmem[0].idx = idx;
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_LOAD;
	shmem[0].tdata2 = (unsigned long)&watched;
	shmem[0].tdata3 = 0;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_UPDATE, 1), SBI_SUCCESS);
	CHECK(hits(dbtr_target) == 0,
	      "breakpoint still there after the update");
	CHECK(hits(read_watched) == 1 && trap_cause == CAUSE_BREAKPOINT,
	      "watchpoint: %lu traps, cause %lu", trap_count, trap_cause);
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_LOAD | MC_CHAIN;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_UPDATE, 1),
		  SBI_ERR_INVALID_PARAM);
	shmem[0].idx = total;
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_LOAD;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_UPDATE, 1),
		  SBI_ERR_INVALID_PARAM);

	/* As many more as there are triggers, and not one beyond. */
	for (unsigned long i = 1; i < total; i++) {
		shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_EXECUTE;
		shmem[0].tdata2 = 0x1000 * i;
		if (sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1).error)
			break;
	}
	shmem[0].tdata1 = TDATA1_TYPE(type) | MC_S | MC_EXECUTE;
	CHECK_RET(sbi_call1(SBI_EXT_DBTR, FID_INSTALL, 1), SBI_ERR_FAILED);

	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_UNINSTALL, idx, 1), SBI_SUCCESS);
	CHECK(hits(read_watched) == 0, "uninstalled trigger fired");
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_UNINSTALL, idx, 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_ENABLE, total, 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_READ, idx, 1), SBI_SUCCESS);
	/* Off: no state, and whatever type the hardware keeps, nothing else. */
	CHECK(shmem[0].idx == 0 && !(shmem[0].tdata1 & ~TDATA1_TYPE(15)) &&
	      shmem[0].tdata2 == 0,
	      "state %lx tdata1 %lx tdata2 %lx after uninstall", shmem[0].idx,
	      shmem[0].tdata1, shmem[0].tdata2);
	CHECK_RET(sbi_call2(SBI_EXT_DBTR, FID_READ, total, 1),
		  SBI_ERR_BAD_RANGE);

	/* Clean up whatever the fill loop installed. */
	for (unsigned long i = 0; i < total; i++)
		sbi_call2(SBI_EXT_DBTR, FID_UNINSTALL, i, 1);
	CHECK_RET(sbi_call3(SBI_EXT_DBTR, FID_SET_SHMEM, ~UL(0), ~UL(0), 0),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call0(SBI_EXT_DBTR, 8), SBI_ERR_NOT_SUPPORTED);
}

#else

void test_dbtr(void)
{
}

#endif
