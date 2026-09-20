// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI SSE tests: the software injected local and global events.
 */

#include <arch/csr.h>
#include <arch/sse.h>
#include <atomic.h>
#include <io.h>
#include <util.h>

#include "sbicall.h"
#include "sbitest.h"

#ifdef CONFIG_SBI_SSE

#define FID_READ_ATTRS 0
#define FID_WRITE_ATTRS 1
#define FID_REGISTER 2
#define FID_UNREGISTER 3
#define FID_ENABLE 4
#define FID_DISABLE 5
#define FID_COMPLETE 6
#define FID_INJECT 7
#define FID_UNMASK 8
#define FID_MASK 9

#define LOCAL SSE_EVENT_LOCAL_SOFTWARE
#define GLOBAL SSE_EVENT_GLOBAL_SOFTWARE
#define SSTATUS_SIE BIT(1)
#define SEPC_SENTINEL 0x5e9c0

/* Entry argument of an event; the first two fields are start.S's. */
struct sse_ctx {
	unsigned long stack_top;
	unsigned long tmp;
	unsigned long event;
	unsigned long runs, hartid, order;
	unsigned long seen_state, seen_sepc, seen_sie;
	/* What to do from inside the handler. */
	unsigned long inject_event;
	struct sse_ctx *inject_ctx;
	unsigned long nested_runs_at_exit;
	unsigned long stack[256];
};

static struct sse_ctx local_ctx[SBITEST_MAX_HARTS], global_ctx;
static unsigned long sequence;

static struct sbiret sse_call(unsigned long fid, unsigned long event)
{
	return sbi_call1(SBI_EXT_SSE, fid, event);
}

static unsigned long attr_read(unsigned long event, unsigned long attr)
{
	static unsigned long val[SBITEST_MAX_HARTS];
	unsigned long *slot = &val[csr_read(sscratch) % SBITEST_MAX_HARTS];
	struct sbiret ret = {};

	ret = sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, event, attr, 1,
		       (unsigned long)slot, 0);
	return ret.error ? ~UL(0) : *slot;
}

static struct sbiret attr_write(unsigned long event, unsigned long attr,
				unsigned long value)
{
	static unsigned long val;

	val = value;
	return sbi_call(SBI_EXT_SSE, FID_WRITE_ATTRS, event, attr, 1,
			(unsigned long)&val, 0);
}

void sse_handler(struct sse_ctx *ctx, unsigned long hartid)
{
	ctx->hartid = hartid;
	ctx->order = atomic_add_ulong(&sequence, 1);
	ctx->seen_state = attr_read(ctx->event, SSE_ATTR_STATUS);
	ctx->seen_sepc = attr_read(ctx->event, SSE_ATTR_INTERRUPTED_SEPC);
	ctx->seen_sie = csr_read(sstatus) & SSTATUS_SIE;

	/* A higher priority event preempts us right here, a lower one waits. */
	if (ctx->inject_event) {
		sbi_call2(SBI_EXT_SSE, FID_INJECT, ctx->inject_event, hartid);
		ctx->nested_runs_at_exit = ctx->inject_ctx->runs;
	}
	WRITE_ONCE(ctx->runs, ctx->runs + 1);
}

static void ctx_init(struct sse_ctx *ctx, unsigned long event)
{
	*ctx = (struct sse_ctx){ .event = event };
	ctx->stack_top = (unsigned long)&ctx->stack[ARRAY_SIZE(ctx->stack)];
}

static struct sbiret sse_setup(unsigned long event, struct sse_ctx *ctx)
{
	ctx_init(ctx, event);
	return sbi_call3(SBI_EXT_SSE, FID_REGISTER, event,
			 (unsigned long)_sse_entry, (unsigned long)ctx);
}

static void test_attrs_and_states(struct sse_ctx *ctx)
{
	static unsigned long buf[SSE_ATTR_COUNT + 1];
	struct sbiret ret = {};

	ret = sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, LOCAL, 0, SSE_ATTR_COUNT,
		       (unsigned long)buf, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(buf[SSE_ATTR_STATUS] == SSE_STATUS_INJECTABLE, "status %lx",
	      buf[SSE_ATTR_STATUS]);
	CHECK(buf[SSE_ATTR_PRIORITY] == 0 && buf[SSE_ATTR_CONFIG] == 0 &&
	      buf[SSE_ATTR_ENTRY_PC] == 0,
	      "reset values");
	CHECK(buf[SSE_ATTR_PREFERRED_HART] == csr_read(sscratch),
	      "preferred hart %lu of a local event",
	      buf[SSE_ATTR_PREFERRED_HART]);

	CHECK_RET(sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, LOCAL, 0,
			   SSE_ATTR_COUNT + 1, (unsigned long)buf, 0),
		  SBI_ERR_BAD_RANGE);
	CHECK_RET(sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, LOCAL, 0, 0,
			   (unsigned long)buf, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, LOCAL, 0, 1,
			   (unsigned long)buf + 1, 0),
		  SBI_ERR_INVALID_ADDRESS);
	CHECK_RET(sbi_call(SBI_EXT_SSE, FID_READ_ATTRS, LOCAL, 0, 1,
			   CONFIG_MONITOR_LOAD_ADDR, 0),
		  SBI_ERR_INVALID_ADDRESS);
	/*
	 * Reserved event ids are invalid; standard ones without a source are
	 * not.
	 */
	CHECK_RET(sse_call(FID_ENABLE, 0x00000002), SBI_ERR_INVALID_PARAM);
	CHECK_RET(sse_call(FID_ENABLE, 0x00000000), SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sse_call(FID_ENABLE, 0xffff4000), SBI_ERR_NOT_SUPPORTED);

	/* UNUSED -> REGISTERED -> ENABLED and back, and nothing else. */
	CHECK_RET(sse_call(FID_ENABLE, LOCAL), SBI_ERR_INVALID_STATE);
	CHECK_RET(sse_call(FID_UNREGISTER, LOCAL), SBI_ERR_INVALID_STATE);
	CHECK_RET(sbi_call3(SBI_EXT_SSE, FID_REGISTER, LOCAL,
			    (unsigned long)_sse_entry + 1, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sse_setup(LOCAL, ctx), SBI_SUCCESS);
	CHECK_RET(sse_setup(LOCAL, ctx), SBI_ERR_INVALID_STATE);
	CHECK(attr_read(LOCAL, SSE_ATTR_ENTRY_ARG) == (unsigned long)ctx,
	      "entry arg");
	CHECK_RET(sse_call(FID_DISABLE, LOCAL), SBI_ERR_INVALID_STATE);

	CHECK_RET(attr_write(LOCAL, SSE_ATTR_PRIORITY, 5), SBI_SUCCESS);
	CHECK(attr_read(LOCAL, SSE_ATTR_PRIORITY) == 5, "priority");
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_CONFIG, 2), SBI_ERR_INVALID_PARAM);
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_ENTRY_PC, 0), SBI_ERR_DENIED);
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_STATUS, 0), SBI_ERR_DENIED);
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_PREFERRED_HART, 0),
		  SBI_ERR_DENIED);
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_INTERRUPTED_A6, 0),
		  SBI_ERR_INVALID_STATE);
	CHECK_RET(attr_write(GLOBAL, SSE_ATTR_PREFERRED_HART,
			     SBITEST_MAX_HARTS),
		  SBI_ERR_INVALID_PARAM);

	CHECK_RET(sse_call(FID_ENABLE, LOCAL), SBI_SUCCESS);
	CHECK_RET(attr_write(LOCAL, SSE_ATTR_PRIORITY, 6),
		  SBI_ERR_INVALID_STATE);
	CHECK((attr_read(LOCAL, SSE_ATTR_STATUS) & SSE_STATUS_STATE_MASK) ==
	      SSE_STATE_ENABLED,
	      "state after enable");
}

static void test_delivery(struct sse_ctx *ctx, unsigned long self)
{
	/* Masked at boot: the event is pending, the handler does not run. */
	CHECK_RET(sse_call(FID_MASK, 0), SBI_ERR_ALREADY_STOPPED);
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, LOCAL, self), SBI_SUCCESS);
	CHECK(ctx->runs == 0, "handler ran on a masked hart");
	CHECK(attr_read(LOCAL, SSE_ATTR_STATUS) & SSE_STATUS_PENDING,
	      "not pending");

	/* Unmasking delivers it, before the call even returns. */
	csr_write(sepc, SEPC_SENTINEL);
	csr_set(sstatus, SSTATUS_SIE);
	CHECK_RET(sse_call(FID_UNMASK, 0), SBI_SUCCESS);
	csr_clear(sstatus, SSTATUS_SIE);
	CHECK(ctx->runs == 1 && ctx->hartid == self, "%lu runs, on hart %lu",
	      ctx->runs, ctx->hartid);
	CHECK((ctx->seen_state & SSE_STATUS_STATE_MASK) == SSE_STATE_RUNNING &&
	      !(ctx->seen_state & SSE_STATUS_PENDING),
	      "status %lx in the handler", ctx->seen_state);
	CHECK(ctx->seen_sepc == SEPC_SENTINEL &&
	      csr_read(sepc) == SEPC_SENTINEL,
	      "sepc %lx in the handler, %lx after", ctx->seen_sepc,
	      csr_read(sepc));
	CHECK(!ctx->seen_sie, "handler entered with interrupts on");
	CHECK((attr_read(LOCAL, SSE_ATTR_STATUS) & SSE_STATUS_STATE_MASK) ==
	      SSE_STATE_ENABLED,
	      "state after completion");
	CHECK_RET(sse_call(FID_UNMASK, 0), SBI_ERR_ALREADY_STARTED);

	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, LOCAL, self), SBI_SUCCESS);
	CHECK(ctx->runs == 2, "%lu runs after a second injection", ctx->runs);
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, LOCAL, SBITEST_MAX_HARTS),
		  SBI_ERR_INVALID_PARAM);
	/* Completion outside a handler does nothing. */
	CHECK_RET(sse_call(FID_COMPLETE, 0), SBI_SUCCESS);
}

static void test_priorities(struct sse_ctx *ctx, unsigned long self)
{
	/* The global event outranks the local one (priority 1 against 5). */
	CHECK_RET(sse_setup(GLOBAL, &global_ctx), SBI_SUCCESS);
	CHECK_RET(attr_write(GLOBAL, SSE_ATTR_PRIORITY, 1), SBI_SUCCESS);
	CHECK_RET(attr_write(GLOBAL, SSE_ATTR_PREFERRED_HART, self),
		  SBI_SUCCESS);
	CHECK_RET(attr_write(GLOBAL, SSE_ATTR_CONFIG, SSE_CONFIG_ONESHOT),
		  SBI_SUCCESS);
	CHECK_RET(sse_call(FID_ENABLE, GLOBAL), SBI_SUCCESS);

	/* Injected from the local handler, it preempts it... */
	ctx->runs = 0;
	ctx->inject_event = GLOBAL;
	ctx->inject_ctx = &global_ctx;
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, LOCAL, self), SBI_SUCCESS);
	CHECK(ctx->runs == 1 && global_ctx.runs == 1,
	      "%lu local, %lu global runs", ctx->runs, global_ctx.runs);
	CHECK(ctx->nested_runs_at_exit == 1 && global_ctx.order > ctx->order,
	      "global event did not preempt the local handler");
	/* ... and being one-shot, it is no longer enabled. */
	CHECK((attr_read(GLOBAL, SSE_ATTR_STATUS) & SSE_STATUS_STATE_MASK) ==
	      SSE_STATE_REGISTERED,
	      "one-shot event still enabled");
	ctx->inject_event = 0;

	/* The other way round the local event waits for the global handler. */
	CHECK_RET(sse_call(FID_ENABLE, GLOBAL), SBI_SUCCESS);
	global_ctx.runs = 0;
	ctx->runs = 0;
	global_ctx.inject_event = LOCAL;
	global_ctx.inject_ctx = ctx;
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, GLOBAL, 0), SBI_SUCCESS);
	CHECK(global_ctx.runs == 1 && ctx->runs == 1,
	      "%lu global, %lu local runs", global_ctx.runs, ctx->runs);
	CHECK(global_ctx.nested_runs_at_exit == 0 &&
	      ctx->order > global_ctx.order,
	      "local event preempted the higher priority handler");
	global_ctx.inject_event = 0;
}

#ifdef CONFIG_SBI_PMU
/*
 * Counter overflow as an event (harts with Sscofpmf): a cycle counter that
 * starts a few thousand counts short of wrapping around.
 */
static void test_pmu_overflow(unsigned long self)
{
	static struct {
		uint64_t overflow_bitmap;
		uint64_t values[64];
	} __aligned(4096) snap;
	static struct sse_ctx ctx;
	const unsigned long base = 3, mask = 0xffff;
	unsigned long idx = 0;
	struct sbiret ret = {};

	ret = sse_setup(SSE_EVENT_LOCAL_PMU_OVERFLOW, &ctx);
	if (ret.error == SBI_ERR_NOT_SUPPORTED) {
		printf("  no counter overflow interrupt on this hart\n");
		return;
	}
	printf("sse (pmu overflow)\n");
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(!(attr_read(SSE_EVENT_LOCAL_PMU_OVERFLOW, SSE_ATTR_STATUS) &
		SSE_STATUS_INJECTABLE),
	      "overflow event injectable from S-mode");
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT,
			    SSE_EVENT_LOCAL_PMU_OVERFLOW, self),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sse_call(FID_ENABLE, SSE_EVENT_LOCAL_PMU_OVERFLOW),
		  SBI_SUCCESS);

	/*
	 * A programmable counter on cycles (the fixed one cannot
	 * overflow-interrupt).
	 */
	ret = sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_CONFIG_MATCHING, base, mask,
		       0, SBI_PMU_HW_CPU_CYCLES, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	idx = (unsigned long)ret.value;
	CHECK(idx >= base, "cycles on counter %lu", idx);
	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_SNAPSHOT_SET_SHMEM,
			    (unsigned long)&snap, 0, 0),
		  SBI_SUCCESS);

	/*
	 * Far enough away that the counter is running by then: QEMU's counter
	 * follows host time, and arms its overflow timer when the value is
	 * written, before the monitor has cleared OF and started the counter.
	 */
	snap.values[idx - base] = ~ULL(0) - 20000000;
	CHECK_RET(sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_START, base,
			   BIT(idx - base), SBI_PMU_START_FLAG_INIT_SNAPSHOT, 0,
			   0),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(ctx.runs), "no overflow event");
	CHECK(ctx.runs == 1 && ctx.hartid == self, "%lu overflow events",
	      ctx.runs);

	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, base,
			    BIT(idx - base),
			    SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT |
			    SBI_PMU_STOP_FLAG_RESET),
		  SBI_SUCCESS);
	CHECK(snap.overflow_bitmap == BIT64(idx - base), "overflow bitmap %llx",
	      (unsigned long long)snap.overflow_bitmap);
	/* QEMU before 9.1 misreads a stopped counter in two halves (RV32). */
	CHECK(snap.values[idx - base] < ULL(0x100000000) ||
	      __RISCV_XLEN__ == 32,
	      "counter did not wrap: %llx",
	      (unsigned long long)snap.values[idx - base]);

	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_SNAPSHOT_SET_SHMEM, ~UL(0),
			    ~UL(0), 0),
		  SBI_SUCCESS);
	CHECK_RET(sse_call(FID_DISABLE, SSE_EVENT_LOCAL_PMU_OVERFLOW),
		  SBI_SUCCESS);
	CHECK_RET(sse_call(FID_UNREGISTER, SSE_EVENT_LOCAL_PMU_OVERFLOW),
		  SBI_SUCCESS);
}
#else
static void test_pmu_overflow(unsigned long self)
{
}
#endif

/* Runs on a secondary hart: take part with the local event. */
void sse_secondary_setup(unsigned long hartid)
{
	csr_write(sscratch, hartid);
	sse_setup(LOCAL, &local_ctx[hartid]);
	sse_call(FID_ENABLE, LOCAL);
	sse_call(FID_UNMASK, 0);
}

void test_sse_remote(unsigned long other)
{
	struct sse_ctx *ctx = &local_ctx[other];

	printf("sse (remote)\n");
	CHECK(WAIT_FOR((attr_read(GLOBAL, SSE_ATTR_STATUS), ctx->stack_top)),
	      "hart %lu did not set its event up", other);
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, LOCAL, other),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(READ_ONCE(ctx->runs) == 1),
	      "local event not handled on hart %lu", other);
	CHECK(READ_ONCE(ctx->hartid) == other, "handled on hart %lu",
	      READ_ONCE(ctx->hartid));

	/* A global event goes to its preferred hart. */
	CHECK_RET(attr_write(GLOBAL, SSE_ATTR_PREFERRED_HART, other),
		  SBI_SUCCESS);
	CHECK_RET(sse_call(FID_ENABLE, GLOBAL), SBI_SUCCESS);
	global_ctx.runs = 0;
	CHECK_RET(sbi_call2(SBI_EXT_SSE, FID_INJECT, GLOBAL, 0), SBI_SUCCESS);
	CHECK(WAIT_FOR(global_ctx.runs == 1), "global event not handled");
	CHECK(global_ctx.hartid == other, "global event handled on hart %lu",
	      global_ctx.hartid);
	CHECK(WAIT_FOR((attr_read(GLOBAL, SSE_ATTR_STATUS) &
			SSE_STATUS_STATE_MASK) == SSE_STATE_REGISTERED),
	      "global event state");
	CHECK_RET(sse_call(FID_UNREGISTER, GLOBAL), SBI_SUCCESS);
}

void test_sse(unsigned long self)
{
	struct sse_ctx *ctx = &local_ctx[self];
	struct sbiret ret = {};

	printf("sse\n");
	csr_write(sscratch, self);
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_SSE);
	CHECK(ret.value != 0, "SSE not probed");

	test_attrs_and_states(ctx);
	test_delivery(ctx, self);
	test_priorities(ctx, self);
	test_pmu_overflow(self);

	CHECK_RET(sse_call(FID_DISABLE, LOCAL), SBI_SUCCESS);
	CHECK_RET(sse_call(FID_UNREGISTER, LOCAL), SBI_SUCCESS);
	CHECK(attr_read(LOCAL, SSE_ATTR_PRIORITY) == 0, "attributes not reset");
	CHECK_RET(sbi_call0(SBI_EXT_SSE, 10), SBI_ERR_NOT_SUPPORTED);
}

#else

void sse_secondary_setup(unsigned long hartid)
{
}

void test_sse_remote(unsigned long other)
{
}

void test_sse(unsigned long self)
{
}

#endif
