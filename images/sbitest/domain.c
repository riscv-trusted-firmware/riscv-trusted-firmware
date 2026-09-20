// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Domains, from inside one: the platform's device tree puts the payload in
 * the domain "untrusted", next to "trusted" (entered from here) and
 * "island" (a hart of its own). Their side is in tdomain.c.
 */

#include <arch/csr.h>
#include <atomic.h>
#include <io.h>
#include <util.h>

#include "sbicall.h"
#include "sbitest.h"
#include "tdomain.h"

#ifdef CONFIG_QEMU_VIRT_DOMAINS

static struct sbiret dom_call(unsigned long fid, unsigned long a0,
			      unsigned long a1)
{
	return sbi_call2(SBI_EXT_FW_DOMAIN, fid, a0, a1);
}

static struct sbiret enter(unsigned long arg)
{
	return dom_call(SBI_FW_DOMAIN_ENTER, DOM_TRUSTED, arg);
}

static long probe(unsigned long addr, bool write)
{
	WRITE_ONCE(trap_cause, 0);
	WRITE_ONCE(trap_expected, true);
	if (write)
		PROBE_INSN("sb zero, 0(%0)", : : "r"(addr) : "memory");
	else
		PROBE_INSN("lb t0, 0(%0)", : : "r"(addr) : "t0");
	WRITE_ONCE(trap_expected, false);
	return (long)trap_cause;
}

/* Is there an FPU, a vector unit? sstatus alone does not tell. */
static bool unit_on(bool vector)
{
	csr_set(sstatus, vector ? SSTATUS_VS : SSTATUS_FS);
	WRITE_ONCE(trap_cause, 0);
	WRITE_ONCE(trap_expected, true);
	/* vsetvli t0, zero, e32, m1, ta, ma / fmv.x.w t0, f8: does it trap? */
	if (vector)
		PROBE_INSN(".4byte 0x0d0072d7");
	else
		PROBE_INSN(".4byte 0xe00402d3");
	WRITE_ONCE(trap_expected, false);
	return !trap_cause;
}

static bool beating(void)
{
	unsigned long beat = READ_ONCE(DOM_SHARED->beat);

	return WAIT_FOR(READ_ONCE(DOM_SHARED->beat) != beat);
}

static void test_trusted(void)
{
	unsigned long regs[14] = {}, units = 0;
	bool has_fp = false, has_vec = false, vec_kept = false;
	struct sbiret ret = {};

	/* The first enter boots it; its exit carries what it found wrong. */
	ret = enter(0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 0, "trusted domain boot checks: %lx", ret.value);
	ret = enter(TCMD(TCMD_ECHO, 14));
	CHECK(!ret.error && ret.value == 43, "echo: %ld %ld", ret.error,
	      ret.value);

	/* What the hart holds for this side is there again afterwards. */
	domain_enter_regs(DOM_TRUSTED, TCMD(TCMD_ECHO, 1), regs);
	for (unsigned int i = 0; i < 12; i++)
		CHECK(regs[i] == 0x500 + i, "s%u = %lx after a domain call", i,
		      regs[i]);
	CHECK(regs[12] == 0 && regs[13] == 4, "echo: %ld %ld", regs[12],
	      regs[13]);

	has_fp = unit_on(false);
	has_vec = unit_on(true);
	if (has_fp)
		fp_set(0x1111);
	if (has_vec)
		vec_set(0x3333);
	units = (unsigned long)enter(TCMD(TCMD_UNITS_SET, 0x2222)).value;
	CHECK(!(units & 3),
	      "the trusted domain saw foreign FP/vector state (%lx)", units);
	CHECK(!(units & BIT(4)) == !has_fp && !(units & BIT(5)) == !has_vec,
	      "units %lx", units);
	if (units & BIT(4))
		CHECK(fp_get() == 0x1111 && fp_get_inv() == 0x1111,
		      "f8/f9 = %lx/%lx", fp_get(), fp_get_inv());
	/*
	 * Vector registers too large for the monitor to keep come back cleared.
	 */
	vec_kept = has_vec && vec_vl();
	if (vec_kept)
		CHECK(vec_get() == 0x3333 && vec_get_inv() == 0x3333,
		      "vl %lu, v8/v31 = %lx/%lx", vec_vl(), vec_get(),
		      vec_get_inv());
	ret = enter(TCMD(TCMD_UNITS_GET, 0x2222));
	CHECK((unsigned long)ret.value == (has_fp | SHIFT_UL(vec_kept, 1)),
	      "trusted FP/vector state: %lx", ret.value);
	printf("  fpu: %s, vector: %s\n", has_fp ? "switched" : "none",
	       vec_kept ? "switched" :
	       has_vec	? "cleared" :
	       "none");
	csr_clear(sstatus, SSTATUS_FS | SSTATUS_VS);

	/*
	 * The S-mode timer is this side's: not lost, not delivered over there.
	 */
	sbi_set_timer(now() + TICKS_SHORT);
	enter(TCMD(TCMD_ECHO, 0));
	CHECK(WAIT_FOR(csr_read(sip) & BIT(IRQ_S_TIMER)),
	      "the timer did not survive a domain call");
	sbi_set_timer(~ULL(0));
}

/*
 * PMU, SSE, DBTR and FWFT state is a domain's own, on a hart two of them
 * use: each side leaves its mark, neither finds the other's.
 */
static void test_services(void)
{
	static unsigned long mem[512] __aligned(4096);
	unsigned long bad = 0, here = 0, there = 0;

	/*
	 * A domain counts what it does itself: what this one burns between two
	 * looks at the other one's instruction counter does not show there.
	 */
	there = (unsigned long)enter(TCMD(TCMD_INSTRET, 0)).value;
	for (here = instret_coarse();
	     ((instret_coarse() - here) & INSTRET_COARSE_MASK) < 0x400;)
		;
	there = ((unsigned long)enter(TCMD(TCMD_INSTRET, 0)).value - there) &
		INSTRET_COARSE_MASK;
	/* A step or two back is QEMU misreading a counter that was stopped. */
	CHECK(there < 0x200 || there > INSTRET_COARSE_MASK - 0x100,
	      "instret: %lx counted over there for 400 here", there);

	bad = services_mark(SERVICES_UNTRUSTED, mem);
	CHECK(!bad, "marking the services: %lx", bad);
	bad = (unsigned long)enter(TCMD(TCMD_SERVICES_SET, 0)).value;
	CHECK(!(bad & 0xff),
	      "the trusted domain found service state that is not its own: %lx",
	      bad & 0xff);
	CHECK(!(bad >> 8), "the trusted domain could not set its own: %lx",
	      bad >> 8);
	bad = services_check(SERVICES_UNTRUSTED, mem);
	CHECK(!bad, "service state lost or changed across a domain call: %lx",
	      bad);
	bad = (unsigned long)enter(TCMD(TCMD_SERVICES_GET, 0)).value;
	CHECK(!bad, "the trusted domain lost its service state: %lx", bad);
	bad = services_check(SERVICES_UNTRUSTED, mem);
	CHECK(!bad, "service state lost or changed, second time: %lx", bad);
	services_clean(mem);
}

static void test_trusted_smp(unsigned long other)
{
	unsigned long ipis = 0;
	long error = 0, value = 0;

	/*
	 * A hart that is not the domain's boot hart stops there on its first
	 * visit, for the domain to start it; from here it just looks busy.
	 */
	secondary_domain_enter(other, TCMD(TCMD_ECHO, 5));
	/* The trusted domain can start it once it is there, and no sooner. */
	do
		value = enter(TCMD(TCMD_START, other)).value;
	while (value == SBI_ERR_INVALID_PARAM ||
	       value == SBI_ERR_INVALID_STATE);
	CHECK(value == SBI_SUCCESS, "trusted: hart start: %ld", value);
	CHECK(sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, other).value ==
	      SBI_HSM_STATE_STARTED,
	      "hart %lu does not look busy", other);
	CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, other, 0, 0),
		  SBI_ERR_ALREADY_AVAILABLE);
	CHECK(WAIT_FOR(secondary_domain_returned(other, &error, &value)),
	      "hart %lu did not come back", other);
	CHECK(READ_ONCE(DOM_SHARED->tsec) == other + 1 &&
	      READ_ONCE(DOM_SHARED->tsec_opaque) == TSEC_OPAQUE,
	      "trusted secondary: hart %lu opaque %lx",
	      READ_ONCE(DOM_SHARED->tsec) - 1,
	      READ_ONCE(DOM_SHARED->tsec_opaque));
	CHECK(!error && value == (long)TSEC_FIRST_EXIT, "enter: %ld %lx", error,
	      value);

	/* An IPI for a hart that is away waits for it. */
	secondary_domain_enter(other, TCMD(TCMD_WAIT, 0));
	CHECK(WAIT_FOR(READ_ONCE(DOM_SHARED->waiting) == other + 1),
	      "hart %lu is not in the trusted domain", other);
	ipis = secondary_ipis(other);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 1, other),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_SFENCE_VMA, 1, other, 0,
			   0x1000, 0),
		  SBI_SUCCESS);
	WRITE_ONCE(DOM_SHARED->release, 1);
	CHECK(WAIT_FOR(secondary_domain_returned(other, &error, &value)) &&
	      !error && value == (long)TCMD_WAIT_DONE,
	      "wait: %ld %lx", error, value);
	CHECK(WAIT_FOR(secondary_ipis(other) == ipis + 1),
	      "the IPI for hart %lu was lost", other);
	secondary_ipis_set(other, ipis);
	WRITE_ONCE(DOM_SHARED->release, 0);
}

static void test_island(unsigned long boot)
{
	unsigned long island = 0;
	struct sbiret ret = {};

	CHECK(WAIT_FOR(READ_ONCE(DOM_SHARED->island_hart)) && beating(),
	      "the island domain does not run");
	if (!READ_ONCE(DOM_SHARED->island_hart))
		return;
	island = READ_ONCE(DOM_SHARED->island_hart) - 1;
	CHECK(island != boot && READ_ONCE(DOM_SHARED->island_boots) == 1,
	      "island: hart %lu, %lu boots", island,
	      READ_ONCE(DOM_SHARED->island_boots));

	/* Its hart does not exist as far as this domain goes. */
	CHECK_RET(sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, island),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, island, 0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 1, island),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_ENTER, DOM_ISLAND, 0),
		  SBI_ERR_INVALID_PARAM);

	ret = dom_call(SBI_FW_DOMAIN_STATE, DOM_ISLAND, 0);
	CHECK(!ret.error && ret.value == 1, "island state: %ld %ld", ret.error,
	      ret.value);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_START, DOM_ISLAND, 0),
		  SBI_ERR_ALREADY_AVAILABLE);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_STOP, DOM_ISLAND, 0), SBI_SUCCESS);
	CHECK(!beating(), "the island domain runs after it was stopped");
	CHECK(dom_call(SBI_FW_DOMAIN_STATE, DOM_ISLAND, 0).value == 0,
	      "island state after stop");
	CHECK_RET(dom_call(SBI_FW_DOMAIN_START, DOM_ISLAND, 0), SBI_SUCCESS);
	CHECK(beating() && READ_ONCE(DOM_SHARED->island_boots) == 2,
	      "the island domain did not start again");
	/*
	 * Quiet from here on: the system suspend test wants the machine to
	 * itself.
	 */
	CHECK_RET(dom_call(SBI_FW_DOMAIN_STOP, DOM_ISLAND, 0), SBI_SUCCESS);
}

void test_domains(unsigned long boot, unsigned long other)
{
	struct sbiret ret = dom_call(SBI_FW_DOMAIN_COUNT, 0, 0);
	long count = ret.value;

	printf("domains\n");
	CHECK(!ret.error && (count == 3 || count == 4), "%ld domains", count);
	ret = dom_call(SBI_FW_DOMAIN_SELF, 0, 0);
	CHECK(!ret.error && ret.value == DOM_UNTRUSTED, "running in domain %ld",
	      ret.value);
	CHECK(sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION,
			SBI_EXT_FW_DOMAIN)
			      .value == 1,
	      "no domain extension");

	/* The others' memory is theirs. */
	CHECK(probe(DOM_TMEM, false) == CAUSE_LOAD_ACCESS &&
	      probe(DOM_TMEM, true) == CAUSE_STORE_ACCESS,
	      "the trusted domain's memory is within reach");
	CHECK(probe(DOM_IMEM, false) == CAUSE_LOAD_ACCESS,
	      "the island domain's memory is within reach");
	CHECK(!probe(DOM_SHARED_PROBE, true),
	      "the shared page is out of reach");
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 1, DOM_TMEM,
			    0),
		  SBI_ERR_INVALID_PARAM);

	CHECK_RET(dom_call(SBI_FW_DOMAIN_ENTER, DOM_UNTRUSTED, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_ENTER, (unsigned long)count, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_STATE, (unsigned long)count, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(dom_call(SBI_FW_DOMAIN_STATE + 1, 0, 0),
		  SBI_ERR_NOT_SUPPORTED);

	test_trusted();
	test_services();
	if (other != ~UL(0))
		test_trusted_smp(other);
	if (count == 4)
		test_island(boot);

	/*
	 * A stopped domain forgets its contexts: the next enter boots it again.
	 */
	CHECK_RET(dom_call(SBI_FW_DOMAIN_STOP, DOM_TRUSTED, 0), SBI_SUCCESS);
	ret = enter(0);
	CHECK(!ret.error && ret.value == 0,
	      "trusted domain, second boot: %ld %lx", ret.error, ret.value);
	CHECK(enter(TCMD(TCMD_ECHO, 2)).value == 7,
	      "echo after the second boot");
	/*
	 * ...and with nothing left of what it had with the monitor's services.
	 */
	ret = enter(TCMD(TCMD_SERVICES_SET, 0));
	CHECK(!ret.error && !ret.value,
	      "trusted domain, second boot: services %lx", ret.value);
}

#else

void test_domains(unsigned long boot, unsigned long other)
{
	/*
	 * Whatever the tree says, the calls are there; alone is the usual
	 * answer.
	 */
	struct sbiret ret = sbi_call0(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_COUNT);

	if (sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_FW_DOMAIN)
	    .value != 1)
		return;
	printf("domains\n");
	CHECK(!ret.error && ret.value >= 1, "%ld domains", ret.value);
	if (ret.value != 1)
		return;
	CHECK(sbi_call0(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_SELF).value == 0,
	      "not the root domain");
	CHECK_RET(sbi_call2(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_ENTER, 0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_ENTER, 1, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_EXIT, 0),
		  SBI_ERR_DENIED);
	CHECK_RET(sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_START, 0),
		  SBI_ERR_ALREADY_AVAILABLE);
}

#endif
