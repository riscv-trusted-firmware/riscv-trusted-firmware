// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Moving a hart between domains, and stopping and starting domains.
 *
 * A context is what S-mode software of one domain owns of one hart: the
 * register file, the supervisor CSRs, the floating-point and vector state,
 * the timer deadline, the MPXY shared memory. The hart runs one context at
 * a time; the others wait, either for the context they entered to exit
 * (CALLING) or to be entered again after they exited (IDLE). Switching is
 * done by the hart itself, from an ecall, under one lock: it is rare.
 *
 * What the monitor's services keep for S-mode per hart (PMU counters, SSE
 * events, debug triggers, FWFT settings) is per domain as well, and its
 * hardware side moves with the context: hart_services_switch_out() / _in().
 * What is not switched stays with the hart: the H extension's CSRs.
 */

#include <arch/domain_state.h>
#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/pmp.h>
#include <arch/sse.h>
#include <atomic.h>
#include <domain.h>
#include <ipi.h>
#include <log.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <util.h>

#define MSTATUS_VS GENMASK_UL(10, 9)
#define MSTATUS_SUM BIT(18)
#define CSR_VLENB 0xc22

#define VEC_BYTES (CONFIG_DOMAIN_CONTEXT_VLEN / 8)

enum context_state {
	CONTEXT_NONE, /* never ran, or its domain was stopped since */
	/* what the hart runs (or ran, when it stopped) */
	CONTEXT_RUNNING,
	/* in domain_enter() until the other side exits */
	CONTEXT_CALLING,
	CONTEXT_IDLE, /* in domain_exit() until it is entered again */
};

struct vec_state {
	unsigned long vstart, vcsr, vl, vtype;
	uint8_t regs[32 * VEC_BYTES];
};

struct domain_context {
	enum context_state state;
	struct domain_context *caller; /* to go back to on exit */
	bool ipi; /* an S-mode IPI came while it waited */
	struct trap_regs regs;
	unsigned long sie, sip, stvec, sscratch, sepc, scause, stval, satp;
	unsigned long scounteren, senvcfg;
	uint64_t timer;
	unsigned long mpxy_shmem;
	uint64_t fp[33]; /* f0..f31, fcsr */
#if VEC_BYTES
	struct vec_state vec;
#endif
};

static struct domain_context contexts[CONFIG_DOMAIN_MAX]
				     [CONFIG_PLATFORM_HART_COUNT];
/* Context states and links, the domains' hart masks, the harts' domain. */
static unsigned long domain_lock = SPINLOCK_UNLOCK;
/* One domain_stop() at a time: two that wait for each other would never end. */
static unsigned long stop_lock = SPINLOCK_UNLOCK;

static struct domain_context *context_of(const struct domain *dom,
					 unsigned int hart)
{
	return &contexts[dom->index][hart];
}

static struct domain *domain_of(const struct domain_context *ctx)
{
	return domain_by_index((unsigned int)((ctx - contexts[0]) /
					      CONFIG_PLATFORM_HART_COUNT));
}

/*
 * The vector unit: none, one whose registers a context can hold, or a larger
 * one.
 */
enum { VEC_NONE, VEC_SWITCHED, VEC_CLEARED };

static unsigned int vector_unit(void)
{
	static unsigned long vlenb;

	if (!(csr_read(misa) & MISA_EXT('V')))
		return VEC_NONE;
	csr_set(mstatus, MSTATUS_VS);
	if (!vlenb) {
		vlenb = csr_read(CSR_VLENB);
		if (vlenb > VEC_BYTES)
			pr_warn("domain: VLEN %lu is above DOMAIN_CONTEXT_VLEN, the vector state does not survive a domain switch\n",
				8 * vlenb);
	}
	return vlenb <= VEC_BYTES ? VEC_SWITCHED : VEC_CLEARED;
}

/*
 * All of it, in use or not: a context that has the unit off can turn it on,
 * and is not to find what another domain left there. Vector registers that
 * do not fit are lost rather than left for the next domain to read.
 */
static void unit_state_save(struct domain_context *ctx)
{
	unsigned long misa = csr_read(misa);

	if (misa & MISA_EXT('F')) {
		csr_set(mstatus, MSTATUS_FS);
		_fp_state_save(ctx->fp, misa & MISA_EXT('D'));
	}
#if VEC_BYTES
	if (vector_unit() == VEC_SWITCHED)
		_vec_state_save(&ctx->vec);
#endif
}

static void unit_state_restore(const struct domain_context *ctx)
{
	unsigned long misa = csr_read(misa);

	if (misa & MISA_EXT('F')) {
		csr_set(mstatus, MSTATUS_FS);
		_fp_state_restore(ctx->fp, misa & MISA_EXT('D'));
	}
	switch (vector_unit()) {
#if VEC_BYTES
	case VEC_SWITCHED:
		_vec_state_restore(&ctx->vec);
		break;
#endif
	case VEC_CLEARED:
		_vec_state_clear();
		break;
	default:
		break;
	}
}

static void context_save(struct domain_context *ctx,
			 const struct trap_regs *regs)
{
	ctx->regs = *regs;
	ctx->sie = csr_read(sie);
	ctx->sip = csr_read(sip);
	ctx->stvec = csr_read(stvec);
	ctx->sscratch = csr_read(sscratch);
	ctx->sepc = csr_read(sepc);
	ctx->scause = csr_read(scause);
	ctx->stval = csr_read(stval);
	ctx->satp = csr_read(satp);
	ctx->scounteren = csr_read(scounteren);
	if (hart_has(HART_FEAT_MENVCFG))
		ctx->senvcfg = csr_read(CSR_SENVCFG);
	ctx->timer = timer_smode_get();
	unit_state_save(ctx);
	hart_services_switch_out();
}

/*
 * The other half: the hart is 'ctx' from here on, 'fresh' when that has not
 * run before. Under the lock.
 */
static void context_restore(struct domain_context *ctx, bool fresh)
{
	struct hart *h = this_hart();
	struct domain *from = h->domain, *to = domain_of(ctx);

	csr_write(sie, ctx->sie);
	csr_write(stvec, ctx->stvec);
	csr_write(sscratch, ctx->sscratch);
	csr_write(sepc, ctx->sepc);
	csr_write(scause, ctx->scause);
	csr_write(stval, ctx->stval);
	csr_write(satp, ctx->satp);
	csr_write(scounteren, ctx->scounteren);
	if (hart_has(HART_FEAT_MENVCFG))
		csr_write(CSR_SENVCFG, ctx->senvcfg);
	unit_state_restore(ctx);

	/* A deadline that has passed fires again at once. */
	timer_smode_restore(ctx->timer);
	csr_clear(mip, MIP_SSIP);
	if ((ctx->sip & MIP_SSIP) || ctx->ipi)
		csr_set(mip, MIP_SSIP);
	ctx->ipi = false;
	ctx->mpxy_shmem = mpxy_hart_shmem_swap(ctx->mpxy_shmem);

	hartmask_clear_atomic(&from->assigned, h->index);
	hartmask_clear_atomic(&to->parked, h->index);
	hartmask_set_atomic(&to->assigned, h->index);
	h->domain = to;
	ctx->state = CONTEXT_RUNNING;
	/* Flushes the address translation caches as well, as satp asks for. */
	pmp_domain_set();
	/*
	 * After sie: the counter overflow interrupt may not be S-mode's here.
	 */
	hart_services_switch_in(fresh);
}

/* The context the hart leaves keeps the MPXY memory the next one swaps out. */
static void context_park(struct domain_context *ctx, enum context_state state,
			 const struct trap_regs *regs)
{
	struct hart *h = this_hart();

	context_save(ctx, regs);
	ctx->state = state;
	hartmask_set_atomic(&h->domain->parked, h->index);
}

/*
 * A context that has never run: the domain's next stage, as a hart start would.
 */
static void context_fresh(struct domain_context *ctx,
			  const struct trap_regs *regs)
{
	const struct domain *dom = domain_of(ctx);
	struct domain_context *caller = ctx->caller;
	unsigned long mstatus = regs->mstatus;

	memset(ctx, 0, sizeof(*ctx));
	ctx->caller = caller;
	mstatus &= ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MPRV | MSTATUS_SIE |
		     MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_VS |
		     MSTATUS_SUM | MSTATUS_MXR | MSTATUS_SDT);
#if __RISCV_XLEN__ == 64
	mstatus &= ~(MSTATUS_MPV | MSTATUS_GVA);
#endif
	ctx->regs.mstatus = mstatus | (dom->next_mode << MSTATUS_MPP_SHIFT);
	ctx->regs.mepc = dom->next_addr;
	ctx->regs.a0 = this_hartid();
	ctx->regs.a1 = dom->next_arg1;
	ctx->stvec = dom->next_addr;
	ctx->timer = ~ULL(0);
	ctx->mpxy_shmem = MPXY_SHMEM_NONE;
#if VEC_BYTES
	/* vill, as out of reset */
	ctx->vec.vtype = BIT(__RISCV_XLEN__ - 1);
#endif
}

/*
 * The hart is parked already; 'to' takes over. With (error, value) for
 * the call it waits in, if it does. Does not return when the hart has to
 * stop for the domain to start it.
 */
static void context_switch(struct domain_context *to, struct trap_regs *regs,
			   long error, unsigned long value)
{
	struct domain *dom = domain_of(to);
	bool fresh = to->state == CONTEXT_NONE;

	if (fresh) {
		context_fresh(to, regs);
		if (dom->boot_hart != (int)this_hart_index()) {
			/*
			 * Only the domain's boot hart boots it: the others are
			 * started. Not a started hart of that domain even for
			 * the moment it takes to stop.
			 */
			to->regs.mstatus = regs->mstatus;
			atomic_store_ulong(&this_hart()->hsm_state,
					   SBI_HSM_STATE_STOP_PENDING);
			context_restore(to, true);
			spin_unlock(&domain_lock);
			hsm_hart_force_stop();
		}
	} else {
		to->regs.a0 = (unsigned long)error;
		to->regs.a1 = value;
	}
	context_restore(to, fresh);
	if (regs)
		*regs = to->regs;
}

static bool caller_waits(const struct domain_context *ctx)
{
	return ctx->caller && ctx->caller->state == CONTEXT_CALLING;
}

long domain_enter(struct trap_regs *regs, struct domain *target,
		  unsigned long arg)
{
	unsigned int self = this_hart_index();
	struct domain_context *cur = NULL, *to = NULL;
	long rc = SBI_SUCCESS;

	spin_lock(&domain_lock);
	cur = context_of(this_domain(), self);
	to = context_of(target, self);
	if (target == this_domain() || !hartmask_test(&target->possible, self))
		rc = SBI_ERR_INVALID_PARAM;
	else if (atomic_load_ulong(&target->stopping) ||
		 atomic_load_ulong(&this_domain()->stopping))
		rc = SBI_ERR_DENIED;
	else if (to->state == CONTEXT_RUNNING || to->state == CONTEXT_CALLING)
		/* It waits for this very context, some calls down. */
		rc = SBI_ERR_ALREADY_AVAILABLE;
	else if (to->state == CONTEXT_NONE &&
		 !domain_range_ok(target, target->next_addr, 4,
				  DOMAIN_PERM_SU_X))
		rc = SBI_ERR_INVALID_ADDRESS;
	if (rc) {
		spin_unlock(&domain_lock);
		return rc;
	}

	context_park(cur, CONTEXT_CALLING, regs);
	to->caller = cur;
	context_switch(to, regs, SBI_SUCCESS, arg);
	spin_unlock(&domain_lock);
	return SBI_SUCCESS;
}

long domain_exit(struct trap_regs *regs, unsigned long value)
{
	unsigned int self = this_hart_index();
	struct domain_context *cur = NULL, *to = NULL;

	spin_lock(&domain_lock);
	cur = context_of(this_domain(), self);
	if (caller_waits(cur)) {
		to = cur->caller;
	} else {
		/*
		 * Booting: the domains that have not had this hart yet, root
		 * last.
		 */
		for (unsigned int d = 1; d < domain_count() && !to; d++) {
			struct domain *dom = domain_by_index(d);

			if (dom != this_domain() &&
			    hartmask_test(&dom->possible, self) &&
			    !atomic_load_ulong(&dom->stopping) &&
			    context_of(dom, self)->state == CONTEXT_NONE)
				to = context_of(dom, self);
		}
		if (!to && this_domain()->index)
			to = context_of(domain_by_index(0), self);
		if (to)
			to->caller = NULL;
	}
	if (!to || atomic_load_ulong(&this_domain()->stopping)) {
		spin_unlock(&domain_lock);
		return SBI_ERR_DENIED;
	}

	cur->caller = NULL;
	context_park(cur, CONTEXT_IDLE, regs);
	context_switch(to, regs, SBI_SUCCESS, value);
	spin_unlock(&domain_lock);
	return SBI_SUCCESS;
}

void domain_context_started(void)
{
	struct domain *dom = this_domain();

	spin_lock(&domain_lock);
	context_of(dom, this_hart_index())->state = CONTEXT_RUNNING;
	spin_unlock(&domain_lock);
	/* domain_stop() got there first: this hart was not to start. */
	if (atomic_load_ulong(&dom->stopping))
		domain_stop_self(NULL);
}

void domain_ipi_parked(unsigned int index)
{
	struct hart *h = hart_by_index(index);
	struct domain *dom = this_domain();

	spin_lock(&domain_lock);
	if (h->domain == dom)
		/* back in the meantime */
		ipi_send(index, IPI_EVENT_SMODE);
	else if (context_of(dom, index)->state != CONTEXT_NONE)
		context_of(dom, index)->ipi = true;
	spin_unlock(&domain_lock);
}

bool domain_running(const struct domain *dom)
{
	for (unsigned int i = 0; hart_by_index(i); i++)
		if (domain_hart_assigned(dom, i) && hart_index_valid(i) &&
		    atomic_load_ulong(&hart_by_index(i)->hsm_state) !=
			    SBI_HSM_STATE_STOPPED)
			return true;
	return domain_has_parked(dom);
}

long domain_start(struct domain *dom)
{
	long rc = SBI_SUCCESS;

	spin_lock(&domain_lock);
	if (atomic_load_ulong(&dom->stopping))
		rc = SBI_ERR_DENIED;
	else if (domain_running(dom))
		rc = SBI_ERR_ALREADY_AVAILABLE;
	else if (dom->boot_hart < 0 ||
		 !domain_hart_assigned(dom, (unsigned int)dom->boot_hart))
		/*
		 * It has no hart of its own to boot on: it runs when it is
		 * entered.
		 */
		rc = SBI_ERR_INVALID_STATE;
	else if (!domain_range_ok(dom, dom->next_addr, 4, DOMAIN_PERM_SU_X))
		rc = SBI_ERR_INVALID_ADDRESS;
	/* What its harts ran when they stopped is history. */
	for (unsigned int i = 0; !rc && hart_by_index(i); i++)
		context_of(dom, i)->state = CONTEXT_NONE;
	spin_unlock(&domain_lock);
	if (!rc)
		sse_domain_reset(dom->index);

	return rc ? rc :
		    hsm_hart_boot((unsigned int)dom->boot_hart, dom->next_addr,
				  dom->next_arg1, dom->next_mode);
}

bool domain_stop_pending(void)
{
	struct domain *dom = this_domain();

	return dom && atomic_load_ulong(&dom->stopping);
}

bool domain_stop_pending_on(unsigned int index)
{
	return atomic_load_ulong(&hart_by_index(index)->domain->stopping);
}

void domain_stop_self(struct trap_regs *regs)
{
	struct domain_context *cur = NULL, *to = NULL;

	spin_lock(&domain_lock);
	cur = context_of(this_domain(), this_hart_index());
	to = caller_waits(cur) ? cur->caller : NULL;
	cur->state = CONTEXT_NONE;
	cur->caller = NULL;
	if (!to) {
		spin_unlock(&domain_lock);
		hsm_hart_force_stop();
	}
	/* It came from another domain: back there, its call failed. */
	hart_services_switch_out();
	context_switch(to, regs, SBI_ERR_FAILED, 0);
	spin_unlock(&domain_lock);
	if (!regs)
		_trap_frame_return(&to->regs);
}

long domain_stop(struct trap_regs *regs, struct domain *dom)
{
	unsigned int self = this_hart_index();
	bool busy = false;

	if (!spin_trylock(&stop_lock))
		return SBI_ERR_FAILED;

	spin_lock(&domain_lock);
	atomic_store_ulong(&dom->stopping, 1);
	/* The contexts that wait go now; the ones that run need their hart. */
	for (unsigned int i = 0; hart_by_index(i); i++) {
		struct domain_context *c = context_of(dom, i);

		if (c->state == CONTEXT_CALLING || c->state == CONTEXT_IDLE) {
			c->state = CONTEXT_NONE;
			c->caller = NULL;
			hartmask_clear_atomic(&dom->parked, i);
		}
	}
	spin_unlock(&domain_lock);

	/* No hart of the domain gets started from here on. */
	hsm_start_barrier();
	for (unsigned int i = 0; hart_by_index(i); i++)
		if (i != self && domain_hart_assigned(dom, i))
			ipi_kick(i);
	do {
		busy = false;
		for (unsigned int i = 0; hart_by_index(i); i++)
			if (i != self && domain_hart_assigned(dom, i) &&
			    hart_index_valid(i) &&
			    atomic_load_ulong(&hart_by_index(i)->hsm_state) !=
				    SBI_HSM_STATE_STOPPED)
				busy = true;
		/*
		 * One of them may wait for this hart in turn (a remote fence).
		 */
		ipi_process();
	} while (busy);

	/*
	 * Entered again, it starts over: nothing is registered, nothing
	 * pending.
	 */
	sse_domain_reset(dom->index);
	atomic_store_ulong(&dom->stopping, 0);
	spin_unlock(&stop_lock);
	if (dom == this_domain())
		domain_stop_self(regs);
	return SBI_SUCCESS;
}
