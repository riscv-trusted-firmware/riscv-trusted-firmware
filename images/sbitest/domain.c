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
#include <types_ext.h>
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

	/* The root domain is all of memory, and nobody's to enter. */
	CHECK_RET(dom_call(SBI_FW_DOMAIN_ENTER, 0, 0), SBI_ERR_DENIED);

	/* The first enter boots it; its exit carries what it found wrong. */
	ret = enter(0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 0, "trusted domain boot checks: %lx", ret.value);
	ret = enter(TCMD(TCMD_ECHO, 14));
	CHECK(!ret.error && ret.value == 43, "echo: %ld %ld", ret.error,
	      ret.value);

	/* The MPXY shared memory is this side's, there or back. */
	if (sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_MPXY)
	    .value) {
		static uint32_t page[1024] __aligned(4096);

		CHECK_RET(sbi_call3(SBI_EXT_MPXY, 1, (unsigned long)page, 0, 0),
			  SBI_SUCCESS);
		CHECK(enter(TCMD(TCMD_MPXY_SHMEM, 0)).value == 1,
		      "the trusted domain came by an MPXY shared memory");
		CHECK_RET(sbi_call1(SBI_EXT_MPXY, 2, 0), SBI_SUCCESS);
		CHECK(enter(TCMD(TCMD_MPXY_SHMEM, 0)).value == 1,
		      "the trusted domain came by an MPXY shared memory, the second time");
		CHECK_RET(sbi_call3(SBI_EXT_MPXY, 1, ~UL(0), ~UL(0), 0),
			  SBI_SUCCESS);
	}

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

	/* A hypervisor's CSRs are its domain's. */
	WRITE_ONCE(trap_cause, 0);
	WRITE_ONCE(trap_expected, true);
	PROBE_INSN("csrr t0, 0x600", ::: "t0"); /* hstatus */
	WRITE_ONCE(trap_expected, false);
	if (!trap_cause) {
		hyp_set(0x1111);
		ret = enter(TCMD(TCMD_HYP_SET, 0x2222));
		CHECK(ret.value == 0,
		      "the trusted domain saw foreign hypervisor CSRs");
		CHECK(hyp_holds(0x1111),
		      "vsscratch %lx, hedeleg %lx after the trusted domain ran",
		      csr_read(CSR_VSSCRATCH), csr_read(CSR_HEDELEG));
		CHECK(enter(TCMD(TCMD_HYP_GET, 0x2222)).value == 1,
		      "the trusted domain's hypervisor CSRs were not kept");
		csr_write(CSR_VSSCRATCH, 0);
		csr_write(CSR_VSTVEC, 0);
		csr_write(CSR_HEDELEG, 0);
		csr_write(CSR_HTIMEDELTA, 0);
	}
	printf("  hypervisor CSRs: %s\n", trap_cause ? "none" : "switched");

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

/* ---- management mode, hosted by the trusted domain ----------------------- */

#define MPXY_SET_SHMEM 1
#define MPXY_READ_ATTRS 3
#define MPXY_SEND_WITH_RESP 5
#define RPMI_MM_GET_ATTRIBUTES 2
#define RPMI_MM_COMMUNICATE 3

static uint32_t mm_page[1024] __aligned(4096);
static unsigned long mm_channel = DOM_CHANNEL_MM;

/*
 * MM_COMMUNICATE of 'size' input bytes: the STATUS, the returned size in *out.
 */
static long mm_communicate(uint32_t in_off, uint32_t size, uint32_t out_off,
			   uint32_t out_size, uint32_t *out)
{
	struct sbiret ret = {};

	mm_page[0] = in_off;
	mm_page[1] = size;
	mm_page[2] = out_off;
	mm_page[3] = out_size;
	ret = sbi_call3(SBI_EXT_MPXY, MPXY_SEND_WITH_RESP, mm_channel,
			RPMI_MM_COMMUNICATE, 16);
	if (ret.error || ret.value != 8)
		return ret.error ? ret.error : -100;
	*out = mm_page[1];
	return (long)(int32_t)mm_page[0];
}

static void mm_round_trip(uint32_t size, unsigned char seed)
{
	vaddr_t in = (vaddr_t)DOM_SHARED + MM_IN_OFFSET;
	vaddr_t out = (vaddr_t)DOM_SHARED + MM_OUT_OFFSET;
	uint32_t got = 0, bad = 0;
	long rc = 0;

	for (uint32_t i = 0; i < size; i++) {
		io_write8(in + i, (uint8_t)(seed + 3 * i));
		io_write8(out + i, 0);
	}
	rc = mm_communicate(MM_IN_OFFSET, size, MM_OUT_OFFSET, MM_AREA_SIZE,
			    &got);
	for (uint32_t i = 0; i < size; i++)
		bad += io_read8(out + i) !=
		       (uint8_t)(io_read8(in + size - 1 - i) ^ MM_XOR);
	CHECK(rc == 0 && got == size && !bad,
	      "MM_COMMUNICATE of %u bytes: status %ld, %u back, %u wrong", size,
	      rc, got, bad);
}

static void test_mm(unsigned long other)
{
	unsigned long flags = 0;
	long error = 0, value = 0;
	uint32_t got = 0;
	uint64_t start = 0;
	struct sbiret ret = {};

	CHECK_RET(sbi_call3(SBI_EXT_MPXY, MPXY_SET_SHMEM,
			    (unsigned long)mm_page, 0, 0),
		  SBI_SUCCESS);
	/*
	 * The channel is this domain's; the one the requests arrive through is
	 * not.
	 */
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, MPXY_READ_ATTRS, DOM_CHANNEL_REQFWD,
			    0, 1),
		  SBI_ERR_NOT_SUPPORTED);
	ret = sbi_call3(SBI_EXT_MPXY, MPXY_READ_ATTRS, DOM_CHANNEL_MM,
			UL(0x80000000), 2);
	CHECK(!ret.error && mm_page[0] == 0xb && mm_page[1] == 0x10000,
	      "MM channel: %ld, group %x %x", ret.error, mm_page[0],
	      mm_page[1]);

	ret = sbi_call3(SBI_EXT_MPXY, MPXY_SEND_WITH_RESP, DOM_CHANNEL_MM,
			RPMI_MM_GET_ATTRIBUTES, 0);
	CHECK(!ret.error && ret.value == 20 && mm_page[0] == 0 &&
	      mm_page[1] == 0x10000 &&
	      mm_page[2] == (uint32_t)(unsigned long)DOM_SHARED &&
	      mm_page[4] == 4096,
	      "MM attributes: %ld, %x at %x+%x", ret.error, mm_page[1],
	      mm_page[2], mm_page[4]);
	CHECK(mm_communicate(MM_IN_OFFSET, 4096, MM_OUT_OFFSET, 4, &got) ==
	      -5 &&
	      mm_communicate(4096, 1, MM_OUT_OFFSET, 4, &got) == -5 &&
	      mm_communicate(MM_IN_OFFSET, 4, 4092, 8, &got) == -5,
	      "MM_COMMUNICATE outside the MM shared memory");

	/* Nobody home: the request waits its time and comes back. */
	start = now();
	CHECK(mm_communicate(MM_IN_OFFSET, 4, MM_OUT_OFFSET, 4, &got) == -12,
	      "MM_COMMUNICATE without a server");
	CHECK(now() - start >= MM_TIMEOUT_US * ULL(10) / 2 &&
	      now() - start < MM_TIMEOUT_US * ULL(10) * 4,
	      "timed out after %lu ticks", (unsigned long)(now() - start));
	if (other == ~UL(0))
		return;

	/* Another hart of ours goes over to be the server: four requests. */
	WRITE_ONCE(DOM_SHARED->mm_dropped, 0);
	secondary_domain_enter(other, TCMD(TCMD_MM_SERVE, 4));
	mm_round_trip(16, 1);
	mm_round_trip(MM_AREA_SIZE, 2);
	/* One it sits on: we give up, and its answer is turned away. */
	CHECK(mm_communicate(MM_IN_OFFSET, MM_DROP_SIZE, MM_OUT_OFFSET, 4,
			     &got) == -12,
	      "a request the server sat on");
	CHECK(WAIT_FOR(READ_ONCE(DOM_SHARED->mm_dropped)) &&
	      (long)READ_ONCE(DOM_SHARED->mm_dropped) - 1 == -14,
	      "the late completion: %ld",
	      (long)READ_ONCE(DOM_SHARED->mm_dropped) - 1);
	mm_round_trip(64, 3);
	CHECK(WAIT_FOR(secondary_domain_returned(other, &error, &value)) &&
	      !error,
	      "the server did not come back: %ld", error);
	flags = (unsigned long)value;
	CHECK((flags & 0xff) == 4 && !(flags & MM_BAD), "the server: %lx",
	      flags);
	CHECK((flags & MM_SAW_MSI) && (flags & MM_SAW_EVENT),
	      "REQFWD_NEW_MESSAGE: %lx", flags);
	CHECK(flags & MM_SAW_SSIP,
	      "no software interrupt for the owner of the queue");
	CHECK(flags & MM_SAW_PIECES,
	      "a 24-byte message in one piece through a 20-byte channel");
	CHECK(flags & MM_SAW_OWN_CHANNELS,
	      "the trusted domain's view of the channels");
	sbi_call3(SBI_EXT_MPXY, MPXY_SET_SHMEM, ~UL(0), ~UL(0), 0);
}

/*
 * The same service through a bridge, which needs no second hart: the one
 * that has the request takes it to the trusted domain and brings the
 * answer back.
 */
static void test_bridge(void)
{
	uint32_t got = 0;
	struct sbiret ret = {};
	long rc = 0;

	printf("  bridge\n");
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, MPXY_SET_SHMEM,
			    (unsigned long)mm_page, 0, 0),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, MPXY_READ_ATTRS, DOM_CHANNEL_BRIDGE,
			    0, 1),
		  SBI_ERR_NOT_SUPPORTED);
	ret = sbi_call3(SBI_EXT_MPXY, MPXY_READ_ATTRS, DOM_CHANNEL_BRIDGE_MM,
			UL(0x80000000), 1);
	CHECK(!ret.error && mm_page[0] == 0xb,
	      "bridged MM channel: %ld, group %x", ret.error, mm_page[0]);
	mm_channel = DOM_CHANNEL_BRIDGE_MM;

	/*
	 * The trusted domain waits in its exit call, not for requests: the
	 * hart goes there, comes back without an answer, and that is an error.
	 */
	rc = mm_communicate(MM_IN_OFFSET, 4, MM_OUT_OFFSET, 4, &got);
	CHECK(rc == -13, "a request nobody completed: %ld", rc);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, 2, 0), SBI_SUCCESS);

	/* Its first look for a request finds none and gives the hart back. */
	ret = enter(TCMD(TCMD_BRIDGE_SERVE, 3));
	CHECK(!ret.error && ret.value == 0, "serving the bridge: %ld %lx",
	      ret.error, ret.value);
	mm_round_trip(16, 5);
	mm_round_trip(MM_AREA_SIZE, 6);
	mm_round_trip(64, 7);
	/*
	 * Through with its three: the next entry finds it back at its commands.
	 */
	ret = enter(TCMD(TCMD_ECHO, 0));
	CHECK(!ret.error && ret.value == 3, "the bridge's server: %ld %lx",
	      ret.error, ret.value);
	CHECK(enter(TCMD(TCMD_ECHO, 5)).value == 16,
	      "the trusted domain after serving");

	mm_channel = DOM_CHANNEL_MM;
	sbi_call3(SBI_EXT_MPXY, MPXY_SET_SHMEM, ~UL(0), ~UL(0), 0);
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
	if (sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_MPXY)
	    .value)
		test_mm(other);
	if (sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_MPXY)
	    .value)
		test_bridge();
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
