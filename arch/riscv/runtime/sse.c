// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Supervisor software events, see <arch/sse.h>.
 *
 * An event is injected where a trap from S/U-mode returns: sse_process()
 * rewrites the register file so that the mret lands in the event handler,
 * after saving what the handler's completion has to put back. Another hart
 * is made to pass through there with an IPI. A per-hart flag tells
 * whether there may be anything to do, so that the common trap exit does
 * not take the lock.
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/pmp.h>
#include <arch/pmu.h>
#include <arch/sse.h>
#include <atomic.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <util.h>

struct sse_event {
	uint32_t id;
	bool pending;
	/* global: where it is, or is to be, handled */
	unsigned long hart;
	/* [STATUS] is the state alone */
	unsigned long attr[SSE_ATTR_COUNT];
};

#define INT_FLAGS_SPP BIT(0)
#define INT_FLAGS_SPIE BIT(1)
#define INT_FLAGS_SPV BIT(2)
#define INT_FLAGS_SPVP BIT(3)
#define INT_FLAGS_SDT BIT(5)
#define INT_FLAGS_VALID                                                    \
	(INT_FLAGS_SPP | INT_FLAGS_SPIE | INT_FLAGS_SPV | INT_FLAGS_SPVP | \
	 INT_FLAGS_SDT)

static const uint32_t local_ids[] = {
	SSE_EVENT_LOCAL_DOUBLE_TRAP,
	SSE_EVENT_LOCAL_PMU_OVERFLOW,
	SSE_EVENT_LOCAL_SOFTWARE,
};

static const uint32_t global_ids[] = { SSE_EVENT_GLOBAL_SOFTWARE };

static unsigned long sse_lock = SPINLOCK_UNLOCK;
static struct sse_event local_events[CONFIG_PLATFORM_HART_COUNT]
				    [ARRAY_SIZE(local_ids)];
static struct sse_event global_events[ARRAY_SIZE(global_ids)];
static bool unmasked[CONFIG_PLATFORM_HART_COUNT];
/* Something may be deliverable on the hart: look, under the lock. */
static unsigned long kick[CONFIG_PLATFORM_HART_COUNT];

static bool is_global(uint32_t id)
{
	return id & 0x8000;
}

static bool event_available(uint32_t id)
{
	if (id == SSE_EVENT_LOCAL_PMU_OVERFLOW)
		return pmu_sse_supported();
	if (id == SSE_EVENT_LOCAL_DOUBLE_TRAP)
		return hart_has(HART_FEAT_SSDBLTRP);
	return true;
}

/* Only the software events are S-mode's to inject. */
static bool event_injectable(uint32_t id)
{
	return id == SSE_EVENT_LOCAL_SOFTWARE ||
	       id == SSE_EVENT_GLOBAL_SOFTWARE;
}

/* The source of an event follows its state: ENABLED or RUNNING means armed. */
static void event_source_update(const struct sse_event *e)
{
	if (e->id == SSE_EVENT_LOCAL_PMU_OVERFLOW)
		pmu_sse_enable(e->attr[SSE_ATTR_STATUS] >= SSE_STATE_ENABLED);
}

/*
 * SBI_SUCCESS with *e set, NOT_SUPPORTED for the rest of the standard events.
 */
static long event_find(unsigned long event_id, unsigned long hartid,
		       struct sse_event **e)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(local_ids); i++)
		if (event_id == local_ids[i]) {
			/* Known, but only there with the hardware behind it. */
			if (!event_available(local_ids[i]))
				return SBI_ERR_NOT_SUPPORTED;
			*e = &local_events[hartid][i];
			return SBI_SUCCESS;
		}
	for (unsigned int i = 0; i < ARRAY_SIZE(global_ids); i++)
		if (event_id == global_ids[i]) {
			*e = &global_events[i];
			return SBI_SUCCESS;
		}

	switch (event_id) {
	case 0x00000000: /* local high priority RAS */
	case 0x00008000: /* global high priority RAS */
	case 0x00100000: /* local low priority RAS */
	case 0x00108000: /* global low priority RAS */
		return SBI_ERR_NOT_SUPPORTED;
	default:
		/* Platform specific events: none. The rest is reserved. */
		return event_id <= UL(0xffffffff) && (event_id & 0x4000) ?
			       SBI_ERR_NOT_SUPPORTED :
			       SBI_ERR_INVALID_PARAM;
	}
}

static void event_reset(struct sse_event *e, uint32_t id, unsigned long hartid)
{
	*e = (struct sse_event){ .id = id, .hart = hartid };
	e->attr[SSE_ATTR_PREFERRED_HART] = hartid;
}

void sse_hart_init(void)
{
	unsigned long self = this_hartid();
	static bool globals_ready;

	spin_lock(&sse_lock);
	unmasked[self] = false;
	for (unsigned int i = 0; i < ARRAY_SIZE(local_ids); i++)
		event_reset(&local_events[self][i], local_ids[i], self);
	/* The first hart to get here is the boot hart. */
	for (unsigned int i = 0; i < ARRAY_SIZE(global_ids) && !globals_ready;
	     i++)
		event_reset(&global_events[i], global_ids[i], self);
	globals_ready = true;
	spin_unlock(&sse_lock);
}

/* Lower value first, then lower event id. */
static bool outranks(const struct sse_event *a, const struct sse_event *b)
{
	if (a->attr[SSE_ATTR_PRIORITY] != b->attr[SSE_ATTR_PRIORITY])
		return a->attr[SSE_ATTR_PRIORITY] < b->attr[SSE_ATTR_PRIORITY];
	return a->id < b->id;
}

/* The top event of 'self' in 'state': pending ones only for ENABLED. */
static struct sse_event *top_event(unsigned long self, unsigned long state)
{
	struct sse_event *best = NULL, *e = NULL;

	for (unsigned int i = 0;
	     i < ARRAY_SIZE(local_ids) + ARRAY_SIZE(global_ids); i++) {
		e = i < ARRAY_SIZE(local_ids) ?
			    &local_events[self][i] :
			    &global_events[i - ARRAY_SIZE(local_ids)];
		if (e->attr[SSE_ATTR_STATUS] != state || e->hart != self ||
		    (state == SSE_STATE_ENABLED && !e->pending))
			continue;
		if (!best || outranks(e, best))
			best = e;
	}
	return best;
}

static void inject(struct sse_event *e, struct trap_regs *regs)
{
	unsigned long mstatus = regs->mstatus, flags = 0;
	bool from_s = get_field_ul(mstatus, MSTATUS_MPP) == PRV_S;

	if (mstatus & MSTATUS_SPP)
		flags |= INT_FLAGS_SPP;
	if (mstatus & MSTATUS_SPIE)
		flags |= INT_FLAGS_SPIE;
#if __RISCV_XLEN__ == 64
	if (hart_has(HART_FEAT_H)) {
		unsigned long hstatus = csr_read(CSR_HSTATUS);

		if (hstatus & HSTATUS_SPV)
			flags |= INT_FLAGS_SPV;
		if (hstatus & HSTATUS_SPVP)
			flags |= INT_FLAGS_SPVP;
		/* The handler runs in HS-mode: note where we came from. */
		hstatus &= ~HSTATUS_SPV;
		if (mstatus & MSTATUS_MPV) {
			hstatus |= HSTATUS_SPV;
			hstatus &= ~HSTATUS_SPVP;
			if (from_s)
				hstatus |= HSTATUS_SPVP;
		}
		csr_write(CSR_HSTATUS, hstatus);
		mstatus &= ~MSTATUS_MPV;
	}
#endif
	/* Ssdbltrp: the handler starts, like a trap handler, unable to trap. */
	if (hart_smode_double_trap_enabled()) {
		if (mstatus & MSTATUS_SDT)
			flags |= INT_FLAGS_SDT;
		mstatus |= MSTATUS_SDT;
	}
	e->attr[SSE_ATTR_INTERRUPTED_FLAGS] = flags;
	e->attr[SSE_ATTR_INTERRUPTED_SEPC] = csr_read(sepc);
	e->attr[SSE_ATTR_INTERRUPTED_A6] = regs->a6;
	e->attr[SSE_ATTR_INTERRUPTED_A7] = regs->a7;

	/* As if S-mode had trapped to the handler from the interrupted code. */
	csr_write(sepc, regs->mepc);
	mstatus &= ~(MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_MPP);
	if (from_s)
		mstatus |= MSTATUS_SPP;
	if (mstatus & MSTATUS_SIE)
		mstatus |= MSTATUS_SPIE;
	mstatus &= ~MSTATUS_SIE;
	mstatus |= SHIFT_UL(PRV_S, MSTATUS_MPP_SHIFT);

	regs->mstatus = mstatus;
	regs->mepc = e->attr[SSE_ATTR_ENTRY_PC];
	regs->a6 = this_hartid();
	regs->a7 = e->attr[SSE_ATTR_ENTRY_ARG];

	e->pending = false;
	e->attr[SSE_ATTR_STATUS] = SSE_STATE_RUNNING;
}

void sse_process(struct trap_regs *regs)
{
	unsigned long self = this_hartid();
	struct sse_event *next = NULL, *running = NULL;

	if (!atomic_load_ulong(&kick[self]))
		return;

	spin_lock(&sse_lock);
	next = top_event(self, SSE_STATE_ENABLED);
	/* Nothing to deliver: no need to look again until someone says so. */
	if (!next)
		atomic_store_ulong(&kick[self], 0);
	running = top_event(self, SSE_STATE_RUNNING);
	if (next && unmasked[self] && (!running || outranks(next, running)))
		inject(next, regs);
	spin_unlock(&sse_lock);
}

bool sse_pending(void)
{
	return atomic_load_ulong(&kick[this_hartid()]);
}

bool sse_complete(struct trap_regs *regs)
{
	unsigned long self = this_hartid(), mstatus = regs->mstatus, flags = 0;
	struct sse_event *e = NULL;

	spin_lock(&sse_lock);
	e = top_event(self, SSE_STATE_RUNNING);
	if (!e) {
		spin_unlock(&sse_lock);
		return false;
	}
	flags = e->attr[SSE_ATTR_INTERRUPTED_FLAGS];

	/* What an sret of the handler would do, then the saved state on top. */
	regs->mepc = csr_read(sepc);
	mstatus &= ~MSTATUS_MPP;
	if (mstatus & MSTATUS_SPP)
		mstatus |= SHIFT_UL(PRV_S, MSTATUS_MPP_SHIFT);
#if __RISCV_XLEN__ == 64
	if (hart_has(HART_FEAT_H)) {
		unsigned long hstatus = csr_read(CSR_HSTATUS);

		if (hstatus & HSTATUS_SPV)
			mstatus |= MSTATUS_MPV;
		hstatus &= ~(HSTATUS_SPV | HSTATUS_SPVP);
		if (flags & INT_FLAGS_SPV)
			hstatus |= HSTATUS_SPV;
		if (flags & INT_FLAGS_SPVP)
			hstatus |= HSTATUS_SPVP;
		csr_write(CSR_HSTATUS, hstatus);
	}
#endif
	if (hart_smode_double_trap_enabled()) {
		mstatus &= ~MSTATUS_SDT;
		if (flags & INT_FLAGS_SDT)
			mstatus |= MSTATUS_SDT;
	}
	mstatus &= ~(MSTATUS_SIE | MSTATUS_SPP);
	/* SIE cannot be set while SDT is. */
	if ((mstatus & MSTATUS_SPIE) && !(mstatus & MSTATUS_SDT))
		mstatus |= MSTATUS_SIE;
	mstatus &= ~MSTATUS_SPIE;
	if (flags & INT_FLAGS_SPIE)
		mstatus |= MSTATUS_SPIE;
	if (flags & INT_FLAGS_SPP)
		mstatus |= MSTATUS_SPP;
	regs->mstatus = mstatus;
	regs->a6 = e->attr[SSE_ATTR_INTERRUPTED_A6];
	regs->a7 = e->attr[SSE_ATTR_INTERRUPTED_A7];
	csr_write(sepc, e->attr[SSE_ATTR_INTERRUPTED_SEPC]);

	e->attr[SSE_ATTR_STATUS] = e->attr[SSE_ATTR_CONFIG] &
						   SSE_CONFIG_ONESHOT ?
					   SSE_STATE_REGISTERED :
					   SSE_STATE_ENABLED;
	event_source_update(e);
	if (e->id == SSE_EVENT_LOCAL_PMU_OVERFLOW)
		pmu_sse_complete();
	/* Whatever this event kept waiting is due now. */
	atomic_store_ulong(&kick[self], 1);
	spin_unlock(&sse_lock);
	return true;
}

/*
 * Where a global event goes: the preferred hart if it listens, else anyone who
 * does.
 */
static unsigned long global_target(const struct sse_event *e)
{
	unsigned long pref = e->attr[SSE_ATTR_PREFERRED_HART],
		      self = this_hartid();
	struct hartmask running = {};

	hsm_interruptible_mask(&running);
	if (unmasked[pref] && hartmask_test(&running, pref))
		return pref;
	if (unmasked[self])
		return self;
	for (unsigned long h = 0; h < CONFIG_PLATFORM_HART_COUNT; h++)
		if (unmasked[h] && hartmask_test(&running, h))
			return h;
	return pref;
}

static void make_pending(struct sse_event *e, unsigned long hartid)
{
	e->pending = true;
	if (is_global(e->id) && e->attr[SSE_ATTR_STATUS] != SSE_STATE_RUNNING) {
		hartid = global_target(e);
		e->hart = hartid;
	} else {
		hartid = e->hart;
	}
	atomic_store_ulong(&kick[hartid], 1);
	/* Our own trap exit is on its way; another hart needs to take one. */
	if (hartid != this_hartid())
		ipi_send(hartid, IPI_EVENT_SSE);
}

/*
 * An event source in the monitor: the event is due on the calling hart.
 * false: nobody will handle it (not enabled, or the hart takes no events).
 */
bool sse_raise_local(uint32_t event_id)
{
	unsigned long self = this_hartid();
	struct sse_event *e = NULL;
	bool taken = false;

	if (event_find(event_id, self, &e))
		return false;
	spin_lock(&sse_lock);
	taken = unmasked[self] && e->attr[SSE_ATTR_STATUS] >= SSE_STATE_ENABLED;
	make_pending(e, self);
	spin_unlock(&sse_lock);
	return taken;
}

long sse_inject(unsigned long event_id, unsigned long hartid)
{
	struct sse_event *e = NULL;
	long rc = 0;

	if (event_id > UL(0xffffffff))
		return SBI_ERR_INVALID_PARAM;
	/* hart_id only matters for a local event. */
	if (is_global((uint32_t)event_id))
		hartid = this_hartid();
	else if (!hart_valid(hartid))
		return SBI_ERR_INVALID_PARAM;
	rc = event_find(event_id, hartid, &e);
	if (rc)
		return rc;
	if (!event_injectable(e->id))
		return SBI_ERR_INVALID_PARAM;

	spin_lock(&sse_lock);
	make_pending(e, hartid);
	spin_unlock(&sse_lock);
	return SBI_SUCCESS;
}

/* A state transition of the calling hart's view of the event. */
static long transition(unsigned long event_id, unsigned long from,
		       unsigned long to)
{
	struct sse_event *e = NULL;
	long rc = event_find(event_id, this_hartid(), &e);

	if (rc)
		return rc;
	spin_lock(&sse_lock);
	if (e->attr[SSE_ATTR_STATUS] != from) {
		rc = SBI_ERR_INVALID_STATE;
	} else {
		e->attr[SSE_ATTR_STATUS] = to;
		event_source_update(e);
		/* Newly enabled and already pending: deliver. */
		if (to == SSE_STATE_ENABLED && e->pending)
			make_pending(e, e->hart);
		if (to == SSE_STATE_UNUSED)
			event_reset(e, e->id,
				    is_global(e->id) ?
				    e->attr[SSE_ATTR_PREFERRED_HART] :
				    this_hartid());
	}
	spin_unlock(&sse_lock);
	return rc;
}

long sse_register(unsigned long event_id, unsigned long entry_pc,
		  unsigned long entry_arg)
{
	struct sse_event *e = NULL;
	long rc = event_find(event_id, this_hartid(), &e);

	if (rc)
		return rc;
	if ((entry_pc & 1) || !smode_range_ok(entry_pc, 4))
		return SBI_ERR_INVALID_PARAM;

	spin_lock(&sse_lock);
	if (e->attr[SSE_ATTR_STATUS] != SSE_STATE_UNUSED) {
		rc = SBI_ERR_INVALID_STATE;
	} else {
		e->attr[SSE_ATTR_ENTRY_PC] = entry_pc;
		e->attr[SSE_ATTR_ENTRY_ARG] = entry_arg;
		e->attr[SSE_ATTR_STATUS] = SSE_STATE_REGISTERED;
	}
	spin_unlock(&sse_lock);
	return rc;
}

long sse_unregister(unsigned long event_id)
{
	return transition(event_id, SSE_STATE_REGISTERED, SSE_STATE_UNUSED);
}

long sse_enable(unsigned long event_id)
{
	return transition(event_id, SSE_STATE_REGISTERED, SSE_STATE_ENABLED);
}

long sse_disable(unsigned long event_id)
{
	return transition(event_id, SSE_STATE_ENABLED, SSE_STATE_REGISTERED);
}

long sse_hart_unmask(void)
{
	unsigned long self = this_hartid();

	if (unmasked[self])
		return SBI_ERR_ALREADY_STARTED;
	unmasked[self] = true;
	/* Events may have piled up meanwhile. */
	atomic_store_ulong(&kick[self], 1);
	return SBI_SUCCESS;
}

long sse_hart_mask(void)
{
	unsigned long self = this_hartid();

	if (!unmasked[self])
		return SBI_ERR_ALREADY_STOPPED;
	unmasked[self] = false;
	return SBI_SUCCESS;
}

/*
 * Attribute memory: XLEN-wide values, which S-mode names by physical address.
 */
static long attrs_check(unsigned long event_id, unsigned long base,
			unsigned long count, unsigned long addr,
			struct sse_event **e)
{
	long rc = event_find(event_id, this_hartid(), e);

	if (rc)
		return rc;
	if (!count)
		return SBI_ERR_INVALID_PARAM;
	if (base >= SSE_ATTR_COUNT || count > SSE_ATTR_COUNT - base)
		return SBI_ERR_BAD_RANGE;
	if (!IS_ALIGNED(addr, sizeof(long)) ||
	    !smode_range_ok(addr, count * sizeof(long)))
		return SBI_ERR_INVALID_ADDRESS;
	return SBI_SUCCESS;
}

long sse_read_attrs(unsigned long event_id, unsigned long base,
		    unsigned long count, unsigned long addr)
{
	unsigned long *out = NULL;
	struct sse_event *e = NULL;
	long rc = attrs_check(event_id, base, count, addr, &e);

	if (rc)
		return rc;
	out = smode_access_begin(addr, count * sizeof(*out));
	spin_lock(&sse_lock);
	for (unsigned long i = 0; i < count; i++) {
		unsigned long val = e->attr[base + i];

		if (base + i == SSE_ATTR_STATUS)
			val |= (e->pending ? SSE_STATUS_PENDING : 0) |
			       (event_injectable(e->id) ?
					SSE_STATUS_INJECTABLE :
					0);
		out[i] = val;
	}
	spin_unlock(&sse_lock);
	smode_access_end();
	return SBI_SUCCESS;
}

static long attr_write_check(const struct sse_event *e, unsigned long id,
			     unsigned long val)
{
	unsigned long state = e->attr[SSE_ATTR_STATUS];
	bool idle = state == SSE_STATE_UNUSED || state == SSE_STATE_REGISTERED;

	switch (id) {
	case SSE_ATTR_PRIORITY:
		if (val > UL(0xffffffff))
			return SBI_ERR_INVALID_PARAM;
		return idle ? SBI_SUCCESS : SBI_ERR_INVALID_STATE;
	case SSE_ATTR_CONFIG:
		if (val & ~SSE_CONFIG_ONESHOT)
			return SBI_ERR_INVALID_PARAM;
		return idle ? SBI_SUCCESS : SBI_ERR_INVALID_STATE;
	case SSE_ATTR_PREFERRED_HART:
		if (!is_global(e->id))
			return SBI_ERR_DENIED;
		if (!hart_valid(val))
			return SBI_ERR_INVALID_PARAM;
		return idle ? SBI_SUCCESS : SBI_ERR_INVALID_STATE;
	case SSE_ATTR_INTERRUPTED_FLAGS:
		if (val & ~INT_FLAGS_VALID)
			return SBI_ERR_INVALID_PARAM;
		fallthrough;
	case SSE_ATTR_INTERRUPTED_SEPC:
	case SSE_ATTR_INTERRUPTED_A6:
	case SSE_ATTR_INTERRUPTED_A7:
		/* Only the hart that runs the handler may change these. */
		return state == SSE_STATE_RUNNING && e->hart == this_hartid() ?
			       SBI_SUCCESS :
			       SBI_ERR_INVALID_STATE;
	default:
		return SBI_ERR_DENIED; /* STATUS, ENTRY_PC, ENTRY_ARG */
	}
}

long sse_write_attrs(unsigned long event_id, unsigned long base,
		     unsigned long count, unsigned long addr)
{
	const unsigned long *in = NULL;
	unsigned long vals[SSE_ATTR_COUNT] = {};
	struct sse_event *e = NULL;
	long rc = attrs_check(event_id, base, count, addr, &e);

	if (rc)
		return rc;
	/* Read once: the memory is S-mode's and can change under us. */
	in = smode_access_begin(addr, count * sizeof(*in));
	for (unsigned long i = 0; i < count; i++)
		vals[i] = in[i];
	smode_access_end();

	spin_lock(&sse_lock);
	for (unsigned long i = 0; i < count && !rc; i++)
		rc = attr_write_check(e, base + i, vals[i]);
	for (unsigned long i = 0; i < count && !rc; i++)
		e->attr[base + i] = vals[i];
	spin_unlock(&sse_lock);
	return rc;
}
