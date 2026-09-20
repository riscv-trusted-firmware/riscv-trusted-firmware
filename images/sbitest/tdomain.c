// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The code of the domains "trusted" and "island". It runs from this image,
 * which those domains can read and execute but not write: no variable of
 * the payload is theirs to use, no printf(), only the stack, their own
 * memory and the shared page.
 */

#include <arch/csr.h>
#include <atomic.h>
#include <io.h>
#include <stdbool.h>
#include <util.h>

#include "sbicall.h"
#include "tdomain.h"

static void say(const char *s)
{
	while (*s)
		sbi_call1(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE,
			  (unsigned char)*s++);
}

static bool unit_on(unsigned long field)
{
	csr_set(sstatus, field);
	return !trusted_unit_probe(field == SSTATUS_VS);
}

static unsigned long units_set(unsigned long x)
{
	unsigned long v = 0;

	if (unit_on(SSTATUS_FS)) {
		v |= BIT(4) | (fp_get() || fp_get_inv() != ~UL(0) ? BIT(0) : 0);
		fp_set(x);
	}
	if (unit_on(SSTATUS_VS)) {
		v |= BIT(5) | (vec_peek() ? BIT(1) : 0);
		vec_set(x);
	}
	return v;
}

static unsigned long units_get(unsigned long x)
{
	unsigned long v = 0;

	if (unit_on(SSTATUS_FS) && fp_get() == x && fp_get_inv() == x)
		v |= BIT(0);
	/* Cleared rather than kept: vtype.vill, which vl = 0 gives away. */
	if (unit_on(SSTATUS_VS) && vec_vl() && vec_get() == x &&
	    vec_get_inv() == x)
		v |= BIT(1);
	return v;
}

/*
 * ---- the monitor's per-hart services, which are per domain ----------------
 */

/* Function ids the monitor keeps to itself. */
#define SBI_SSE_READ_ATTRS 0
#define SBI_SSE_REGISTER 2
#define SBI_SSE_UNREGISTER 3
#define SBI_DBTR_NUM_TRIGGERS 0
#define SBI_DBTR_SET_SHMEM 1
#define SBI_DBTR_READ 2
#define SBI_DBTR_INSTALL 3
#define SBI_DBTR_UNINSTALL 5

#define FW_COUNTER 32 /* the first firmware counter */
#define FW_EVENT ((SBI_PMU_EVENT_TYPE_FW << 16) | SBI_PMU_FW_SET_TIMER)
#define MARK_ADDR(who) (0x1000 + 8 * (who))
#define TDATA1_EXEC_S \
	(SHIFT_UL(2, __RISCV_XLEN__ - 4) | BIT(2) | BIT(4)) /* mcontrol */
#define DBTR_MAPPED BIT(0)

static long sse_attr(unsigned long attr, unsigned long *mem)
{
	mem[0] = ~UL(0);
	if (sbi_call(SBI_EXT_SSE, SBI_SSE_READ_ATTRS, UL(0xffff0000), attr, 1,
		     (unsigned long)mem, 0)
		    .error)
		return -1;
	return (long)mem[0];
}

static bool have(unsigned long eid)
{
	return sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, eid).value;
}

static unsigned long triggers(unsigned long *mem)
{
	if (!have(SBI_EXT_DBTR) || sbi_call3(SBI_EXT_DBTR, SBI_DBTR_SET_SHMEM,
					     (unsigned long)mem, 0, 0)
					   .error)
		return 0;
	return (unsigned long)sbi_call1(SBI_EXT_DBTR, SBI_DBTR_NUM_TRIGGERS, 0)
		.value;
}

/* Trigger 'i' the way the hardware has it: mem[0..3] = state, tdata1-3. */
static bool trigger_read(unsigned long i, unsigned long *mem)
{
	return !sbi_call2(SBI_EXT_DBTR, SBI_DBTR_READ, i, 1).error;
}

/* Nothing of anybody's: what a domain finds on a hart it has not used. */
unsigned long services_pristine(unsigned long *mem)
{
	unsigned long bad = 0, n = 0;

	if (sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET, SBI_FWFT_MISALIGNED_EXC_DELEG)
	    .value)
		bad |= BIT(0);
	if (have(SBI_EXT_PMU) &&
	    sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ, FW_COUNTER).error !=
		    SBI_ERR_INVALID_PARAM)
		bad |= BIT(1);
	if (have(SBI_EXT_SSE) && (sse_attr(0, mem) & 3) != 0)
		bad |= BIT(2);
	n = triggers(mem);
	for (unsigned long i = 0; i < n; i++)
		if (!trigger_read(i, mem) || (mem[0] & DBTR_MAPPED) || mem[2])
			bad |= BIT(3);
	return bad;
}

unsigned long services_mark(unsigned long who, unsigned long *mem)
{
	unsigned long bad = 0;

	long rc = 0;

	/*
	 * The untrusted side has the feature off and locked (the FWFT test saw
	 * to that, if this does not), which the other one is not to notice.
	 */
	if (who == SERVICES_TRUSTED)
		rc = sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
			       SBI_FWFT_MISALIGNED_EXC_DELEG, 1, 0)
			     .error;
	else
		rc = sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
			       SBI_FWFT_MISALIGNED_EXC_DELEG, 0,
			       SBI_FWFT_SET_FLAG_LOCK)
			     .error;
	if (rc && (who == SERVICES_TRUSTED || rc != SBI_ERR_DENIED_LOCKED))
		bad |= BIT(0);
	if (have(SBI_EXT_PMU)) {
		/*
		 * A firmware counter that counts set_timer calls: 'who' of
		 * them.
		 */
		if (sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_CONFIG_MATCHING,
			     FW_COUNTER, 1,
			     SBI_PMU_CFG_FLAG_CLEAR_VALUE |
			     SBI_PMU_CFG_FLAG_AUTO_START,
			     FW_EVENT, 0)
			    .value != FW_COUNTER)
			bad |= BIT(1);
		for (unsigned long i = 0; i < who; i++)
			sbi_call2(SBI_EXT_TIME, SBI_TIME_SET_TIMER, ~UL(0),
				  ~UL(0));
	}
	if (have(SBI_EXT_SSE) &&
	    sbi_call3(SBI_EXT_SSE, SBI_SSE_REGISTER, UL(0xffff0000),
		      CONFIG_SBITEST_LOAD_ADDR, who)
		    .error)
		bad |= BIT(2);
	if (triggers(mem)) {
		mem[0] = 0;
		mem[1] = TDATA1_EXEC_S;
		mem[2] = MARK_ADDR(who);
		mem[3] = 0;
		if (sbi_call1(SBI_EXT_DBTR, SBI_DBTR_INSTALL, 1).error)
			bad |= BIT(3);
	}
	return bad;
}

unsigned long services_check(unsigned long who, unsigned long *mem)
{
	unsigned long bad = 0, n = 0, mine = 0;
	bool trusted = false;
	long rc = 0;

	rc = sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
		       SBI_FWFT_MISALIGNED_EXC_DELEG, 1, 0)
		     .error;
	trusted = who == SERVICES_TRUSTED;
	if (sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET, SBI_FWFT_MISALIGNED_EXC_DELEG)
	    .value != trusted ||
	    rc != (who == SERVICES_TRUSTED ? SBI_SUCCESS :
		   SBI_ERR_DENIED_LOCKED))
		bad |= BIT(0);
	if (have(SBI_EXT_PMU) &&
	    sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ, FW_COUNTER).value !=
		    (long)who)
		bad |= BIT(1);
	if (have(SBI_EXT_SSE) &&
	    ((sse_attr(0, mem) & 3) != 1 || sse_attr(5, mem) != (long)who))
		bad |= BIT(2);
	n = triggers(mem);
	for (unsigned long i = 0; i < n; i++) {
		if (!trigger_read(i, mem))
			bad |= BIT(3);
		else if (mem[0] & DBTR_MAPPED)
			mine += mem[2] == MARK_ADDR(who) ? 1 : 2;
	}
	if (n && mine != 1)
		bad |= BIT(3);
	return bad;
}

void services_clean(unsigned long *mem)
{
	if (have(SBI_EXT_PMU))
		sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, FW_COUNTER, 1,
			  SBI_PMU_STOP_FLAG_RESET);
	if (have(SBI_EXT_SSE))
		sbi_call1(SBI_EXT_SSE, SBI_SSE_UNREGISTER, UL(0xffff0000));
	if (triggers(mem)) {
		sbi_call2(SBI_EXT_DBTR, SBI_DBTR_UNINSTALL, 0, ~UL(0));
		sbi_call3(SBI_EXT_DBTR, SBI_DBTR_SET_SHMEM, ~UL(0), ~UL(0), 0);
	}
}

unsigned long instret_coarse(void)
{
	return (csr_read(instret) >> 16) & INSTRET_COARSE_MASK;
}

/* Exit with 'value', and again with the answer to every command that comes. */
static void __noreturn trusted_serve(unsigned long hartid, unsigned long value)
{
	/*
	 * A page of its own for every hart, past the word trusted_main() uses.
	 */
	unsigned long *scratch =
		(unsigned long *)(DOM_TMEM + 0x1000 * (hartid + 1));

	for (;;) {
		struct sbiret ret =
			sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_EXIT, value);
		struct sbiret started = {};
		unsigned long param = (unsigned long)ret.value >> 8;

		/*
		 * An exit that fails has nowhere to go: say so the only way
		 * left.
		 */
		if (ret.error) {
			say("  trusted: exit failed\n");
			value = ~UL(0);
			continue;
		}
		switch (ret.value & 0xff) {
		case TCMD_ECHO:
			value = 3 * param + 1;
			break;
		case TCMD_UNITS_SET:
			value = units_set(param);
			break;
		case TCMD_UNITS_GET:
			value = units_get(param);
			break;
		case TCMD_WAIT:
			WRITE_ONCE(DOM_SHARED->waiting, hartid + 1);
			while (!READ_ONCE(DOM_SHARED->release))
				;
			WRITE_ONCE(DOM_SHARED->waiting, 0);
			value = TCMD_WAIT_DONE;
			break;
		case TCMD_SERVICES_SET:
			value = services_pristine(scratch) |
				services_mark(SERVICES_TRUSTED, scratch) << 8;
			break;
		case TCMD_SERVICES_GET:
			value = services_check(SERVICES_TRUSTED, scratch);
			break;
		case TCMD_INSTRET:
			value = instret_coarse();
			break;
		case TCMD_START:
			started = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START,
					    param,
					    CONFIG_SBITEST_LOAD_ADDR + 12,
					    TSEC_OPAQUE);
			value = (unsigned long)started.error;
			break;
		default:
			value = ~UL(0);
			break;
		}
	}
}

void trusted_main(unsigned long hartid, unsigned long arg1)
{
	unsigned long *mine = (unsigned long *)DOM_TMEM;
	unsigned long fail = 0;

	say("  trusted: booting\n");
	if (csr_read(satp) || csr_read(sie))
		fail |= BIT(0);
	if (csr_read(sstatus) & (BIT(1) | SSTATUS_FS | SSTATUS_VS))
		fail |= BIT(1);
	WRITE_ONCE(*mine, 0x7e57 + hartid);
	if (READ_ONCE(*mine) != 0x7e57 + hartid)
		fail |= BIT(2);
	if (sbi_call0(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_SELF).value !=
	    DOM_TRUSTED)
		fail |= BIT(3);
	/* Not the system's master: no reset, no say over other domains. */
	if (sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET,
		      SBI_SRST_TYPE_SHUTDOWN, 0)
		    .error != SBI_ERR_DENIED)
		fail |= BIT(4);
	if (sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_STOP, DOM_UNTRUSTED)
	    .error != SBI_ERR_DENIED)
		fail |= BIT(5);
	/* Its memory and the shared page, the image read-only, and no more. */
	if (trusted_probe(DOM_IMEM, 0) != CAUSE_LOAD_ACCESS)
		fail |= BIT(6);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR + CONFIG_SBITEST_SIZE, 0) !=
	    CAUSE_LOAD_ACCESS)
		fail |= BIT(7);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR, 1) != CAUSE_STORE_ACCESS)
		fail |= BIT(8);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR, 0) ||
	    trusted_probe(DOM_SHARED_PROBE, 1))
		fail |= BIT(9);
	/* Nor can it hand the monitor an address that is not its own. */
	if (sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 1, DOM_IMEM, 0)
	    .error != SBI_ERR_INVALID_PARAM)
		fail |= BIT(10);
	/* The island's hart is not one it can see. */
	if (READ_ONCE(DOM_SHARED->island_hart) &&
	    sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS,
		      READ_ONCE(DOM_SHARED->island_hart) - 1)
			    .error != SBI_ERR_INVALID_PARAM)
		fail |= BIT(11);
	trusted_serve(hartid, fail);
}

void trusted_secondary_main(unsigned long hartid, unsigned long opaque)
{
	WRITE_ONCE(DOM_SHARED->tsec_opaque, opaque);
	WRITE_ONCE(DOM_SHARED->tsec, hartid + 1);
	trusted_serve(hartid, TSEC_FIRST_EXIT);
}

void island_main(unsigned long hartid)
{
	WRITE_ONCE(DOM_SHARED->island_hart, hartid + 1);
	atomic_add_ulong(&DOM_SHARED->island_boots, 1);
	for (;;)
		atomic_add_ulong(&DOM_SHARED->beat, 1);
}
