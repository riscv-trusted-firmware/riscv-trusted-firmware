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
 *
 * On a hart with the H extension the context is a hypervisor's, guests and
 * all: the hypervisor CSRs and the VS-level ones of the guest it had set
 * up. A context is only ever left from HS-mode (by a call) or for good (its
 * domain is stopped), so the guest itself is never what is suspended. The
 * guest address translation caches are flushed, since two domains' VMIDs
 * are the same numbers.
 */

#include <arch/domain_state.h>
#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/pmp.h>
#include <arch/sse.h>
#include <atomic.h>
#include <domain.h>
#include <heap.h>
#include <ipi.h>
#include <log.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <util.h>

/*
 * The H extension's CSRs of a context, all XLEN wide: the AIA's from
 * HYP_FIRST_AIA on, and on RV32 the upper halves of its interrupt CSRs.
 * Apart from these: htimedelta, henvcfg, hstateen0 and vstimecmp, 64 bits
 * on RV32 as well, and hip, hgeip and vsip, which are views (hvip has the
 * bits that can be written). A guest interrupt file of an IMSIC is a
 * device, and the PMP's to keep apart.
 */
enum hyp_csr {
	HYP_HSTATUS,
	HYP_HEDELEG,
	HYP_HIDELEG,
	HYP_HIE,
	HYP_HCOUNTEREN,
	HYP_HGEIE,
	HYP_HTVAL,
	HYP_HTINST,
	HYP_HVIP,
	HYP_HGATP,
	HYP_VSSTATUS,
	HYP_VSIE,
	HYP_VSTVEC,
	HYP_VSSCRATCH,
	HYP_VSEPC,
	HYP_VSCAUSE,
	HYP_VSTVAL,
	HYP_VSATP,
	HYP_FIRST_AIA,
	HYP_HVIEN = HYP_FIRST_AIA,
	HYP_HVICTL,
	HYP_HVIPRIO1,
	HYP_HVIPRIO2,
	HYP_VSISELECT,
#if __RISCV_XLEN__ == 32
	HYP_HIDELEGH,
	HYP_HVIENH,
	HYP_HVIPH,
	HYP_HVIPRIO1H,
	HYP_HVIPRIO2H,
	HYP_VSIEH,
#endif
	HYP_CSR_COUNT,
};

#define MSTATUS_VS GENMASK_UL(10, 9)
#define MSTATUS_SUM BIT(18)
#define CSR_VLENB 0xc22

enum context_state {
	CONTEXT_NONE, /* never ran, or its domain was stopped since */
	/* what the hart runs (or ran, when it stopped) */
	CONTEXT_RUNNING,
	/* in domain_enter() until the other side exits */
	CONTEXT_CALLING,
	CONTEXT_IDLE, /* in domain_exit() until it is entered again */
};

/* As large as the boot hart's vector registers are (vec_bytes each). */
struct vec_state {
	unsigned long vstart, vcsr, vl, vtype;
	uint8_t regs[];
};

struct domain_context {
	enum context_state state;
	struct domain_context *caller; /* to go back to on exit */
	bool ipi; /* an S-mode IPI came while it waited */
	/* Left by domain_switch_*(): its registers are what it is to find. */
	bool quiet;
	struct trap_regs regs;
	unsigned long sie, sip, stvec, sscratch, sepc, scause, stval, satp;
	unsigned long scounteren, senvcfg;
	unsigned long hyp[HYP_CSR_COUNT];
	uint64_t htimedelta, henvcfg, hstateen0, vstimecmp;
	uint64_t timer;
	unsigned long mpxy_shmem;
	uint64_t fp[33]; /* f0..f31, fcsr */
	struct vec_state *vec; /* NULL: no vector unit */
};

/*
 * Per domain and hart (domain_hart_alloc()), once domains_start() knows them
 * all.
 */
static struct domain_context *contexts;
/* Context states and links, the domains' hart masks, the harts' domain. */
static unsigned long domain_lock = SPINLOCK_UNLOCK;
/* One domain_stop() at a time: two that wait for each other would never end. */
static unsigned long stop_lock = SPINLOCK_UNLOCK;

static struct domain_context *context_of(const struct domain *dom,
					 unsigned int hart)
{
	return domain_hart_slot(contexts, sizeof(*contexts), dom->index, hart);
}

static struct domain *domain_of(const struct domain_context *ctx)
{
	size_t slot = (size_t)(ctx - contexts) / hart_table_size();

	return domain_by_index((unsigned int)slot);
}

/* VLENB of the boot hart: what a context has room for. 0: no vector unit. */
static unsigned long vec_bytes;

void domain_contexts_init(void)
{
	size_t vec_size = 0;

	if (csr_read(misa) & MISA_EXT('V')) {
		csr_set(mstatus, MSTATUS_VS);
		vec_bytes = csr_read(CSR_VLENB);
		vec_size = sizeof(struct vec_state) + 32 * vec_bytes;
	}
	contexts = domain_hart_alloc(sizeof(*contexts));
	for (unsigned int i = 0;
	     vec_size && i < domain_keys() * hart_table_size(); i++)
		contexts[i].vec = heap_alloc(vec_size);
}

/*
 * The vector unit: none, one whose registers a context can hold, or a larger
 * one.
 */
enum { VEC_NONE, VEC_SWITCHED, VEC_CLEARED };

static unsigned int vector_unit(void)
{
	if (!(csr_read(misa) & MISA_EXT('V')))
		return VEC_NONE;
	csr_set(mstatus, MSTATUS_VS);
	/*
	 * A hart with larger registers than the boot hart's: none known, but.
	 */
	return vec_bytes && csr_read(CSR_VLENB) <= vec_bytes ? VEC_SWITCHED :
							       VEC_CLEARED;
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
	if (vector_unit() == VEC_SWITCHED)
		_vec_state_save(ctx->vec);
}

static void unit_state_restore(const struct domain_context *ctx)
{
	unsigned long misa = csr_read(misa);

	if (misa & MISA_EXT('F')) {
		csr_set(mstatus, MSTATUS_FS);
		_fp_state_restore(ctx->fp, misa & MISA_EXT('D'));
	}
	switch (vector_unit()) {
	case VEC_SWITCHED:
		_vec_state_restore(ctx->vec);
		break;
	case VEC_CLEARED:
		_vec_state_clear();
		break;
	default:
		break;
	}
}

/* hfence.gvma and hfence.vvma, everything: see rfence.c for the encoding. */
static void guest_tlb_flush(void)
{
	__asm__ __volatile__(".insn r 0x73, 0, 0x31, x0, x0, x0" ::: "memory");
	__asm__ __volatile__(".insn r 0x73, 0, 0x11, x0, x0, x0" ::: "memory");
}

#if __RISCV_XLEN__ == 32
#define csr_read64(csr) \
	reg_pair_to_64((uint32_t)csr_read(csr##H), (uint32_t)csr_read(csr))
#define csr_write64(csr, val)                             \
	do {                                              \
		uint64_t __v64 = (val);                   \
							  \
		csr_write(csr, low32_from_64(__v64));     \
		csr_write(csr##H, high32_from_64(__v64)); \
	} while (0)
#else
#define csr_read64(csr) csr_read(csr)
#define csr_write64(csr, val) csr_write(csr, val)
#endif

static unsigned long hyp_csr_read(enum hyp_csr csr)
{
	switch (csr) {
	case HYP_HSTATUS:
		return csr_read(CSR_HSTATUS);
	case HYP_HEDELEG:
		return csr_read(CSR_HEDELEG);
	case HYP_HIDELEG:
		return csr_read(CSR_HIDELEG);
	case HYP_HIE:
		return csr_read(CSR_HIE);
	case HYP_HCOUNTEREN:
		return csr_read(CSR_HCOUNTEREN);
	case HYP_HGEIE:
		return csr_read(CSR_HGEIE);
	case HYP_HTVAL:
		return csr_read(CSR_HTVAL);
	case HYP_HTINST:
		return csr_read(CSR_HTINST);
	case HYP_HVIP:
		return csr_read(CSR_HVIP);
	case HYP_HGATP:
		return csr_read(CSR_HGATP);
	case HYP_VSSTATUS:
		return csr_read(CSR_VSSTATUS);
	case HYP_VSIE:
		return csr_read(CSR_VSIE);
	case HYP_VSTVEC:
		return csr_read(CSR_VSTVEC);
	case HYP_VSSCRATCH:
		return csr_read(CSR_VSSCRATCH);
	case HYP_VSEPC:
		return csr_read(CSR_VSEPC);
	case HYP_VSCAUSE:
		return csr_read(CSR_VSCAUSE);
	case HYP_VSTVAL:
		return csr_read(CSR_VSTVAL);
	case HYP_VSATP:
		return csr_read(CSR_VSATP);
	case HYP_HVIEN:
		return csr_read(CSR_HVIEN);
	case HYP_HVICTL:
		return csr_read(CSR_HVICTL);
	case HYP_HVIPRIO1:
		return csr_read(CSR_HVIPRIO1);
	case HYP_HVIPRIO2:
		return csr_read(CSR_HVIPRIO2);
	case HYP_VSISELECT:
		return csr_read(CSR_VSISELECT);
#if __RISCV_XLEN__ == 32
	case HYP_HIDELEGH:
		return csr_read(CSR_HIDELEGH);
	case HYP_HVIENH:
		return csr_read(CSR_HVIENH);
	case HYP_HVIPH:
		return csr_read(CSR_HVIPH);
	case HYP_HVIPRIO1H:
		return csr_read(CSR_HVIPRIO1H);
	case HYP_HVIPRIO2H:
		return csr_read(CSR_HVIPRIO2H);
	case HYP_VSIEH:
		return csr_read(CSR_VSIEH);
#endif
	default:
		return 0;
	}
}

static void hyp_csr_write(enum hyp_csr csr, unsigned long val)
{
	switch (csr) {
	case HYP_HSTATUS:
		csr_write(CSR_HSTATUS, val);
		break;
	case HYP_HEDELEG:
		csr_write(CSR_HEDELEG, val);
		break;
	case HYP_HIDELEG:
		csr_write(CSR_HIDELEG, val);
		break;
	case HYP_HIE:
		csr_write(CSR_HIE, val);
		break;
	case HYP_HCOUNTEREN:
		csr_write(CSR_HCOUNTEREN, val);
		break;
	case HYP_HGEIE:
		csr_write(CSR_HGEIE, val);
		break;
	case HYP_HTVAL:
		csr_write(CSR_HTVAL, val);
		break;
	case HYP_HTINST:
		csr_write(CSR_HTINST, val);
		break;
	case HYP_HVIP:
		csr_write(CSR_HVIP, val);
		break;
	case HYP_HGATP:
		csr_write(CSR_HGATP, val);
		break;
	case HYP_VSSTATUS:
		csr_write(CSR_VSSTATUS, val);
		break;
	case HYP_VSIE:
		csr_write(CSR_VSIE, val);
		break;
	case HYP_VSTVEC:
		csr_write(CSR_VSTVEC, val);
		break;
	case HYP_VSSCRATCH:
		csr_write(CSR_VSSCRATCH, val);
		break;
	case HYP_VSEPC:
		csr_write(CSR_VSEPC, val);
		break;
	case HYP_VSCAUSE:
		csr_write(CSR_VSCAUSE, val);
		break;
	case HYP_VSTVAL:
		csr_write(CSR_VSTVAL, val);
		break;
	case HYP_VSATP:
		csr_write(CSR_VSATP, val);
		break;
	case HYP_HVIEN:
		csr_write(CSR_HVIEN, val);
		break;
	case HYP_HVICTL:
		csr_write(CSR_HVICTL, val);
		break;
	case HYP_HVIPRIO1:
		csr_write(CSR_HVIPRIO1, val);
		break;
	case HYP_HVIPRIO2:
		csr_write(CSR_HVIPRIO2, val);
		break;
	case HYP_VSISELECT:
		csr_write(CSR_VSISELECT, val);
		break;
#if __RISCV_XLEN__ == 32
	case HYP_HIDELEGH:
		csr_write(CSR_HIDELEGH, val);
		break;
	case HYP_HVIENH:
		csr_write(CSR_HVIENH, val);
		break;
	case HYP_HVIPH:
		csr_write(CSR_HVIPH, val);
		break;
	case HYP_HVIPRIO1H:
		csr_write(CSR_HVIPRIO1H, val);
		break;
	case HYP_HVIPRIO2H:
		csr_write(CSR_HVIPRIO2H, val);
		break;
	case HYP_VSIEH:
		csr_write(CSR_VSIEH, val);
		break;
#endif
	default:
		break;
	}
}

static void hyp_state_save(struct domain_context *ctx)
{
	unsigned int last = hart_has(HART_FEAT_AIA) ? HYP_CSR_COUNT :
						      HYP_FIRST_AIA;

	if (!hart_has(HART_FEAT_H))
		return;
	for (unsigned int i = 0; i < last; i++)
		ctx->hyp[i] = hyp_csr_read(i);
	ctx->htimedelta = csr_read64(CSR_HTIMEDELTA);
	if (hart_has(HART_FEAT_MENVCFG))
		ctx->henvcfg = csr_read64(CSR_HENVCFG);
	if (hart_has(HART_FEAT_SMSTATEEN))
		ctx->hstateen0 = csr_read64(CSR_HSTATEEN0);
	if (hart_has(HART_FEAT_SSTC))
		ctx->vstimecmp = csr_read64(CSR_VSTIMECMP);
	/* What the guests of this context have cached, under its hgatp. */
	guest_tlb_flush();
}

static void hyp_state_restore(const struct domain_context *ctx)
{
	unsigned int last = hart_has(HART_FEAT_AIA) ? HYP_CSR_COUNT :
						      HYP_FIRST_AIA;

	if (!hart_has(HART_FEAT_H))
		return;
	/*
	 * No guest timer interrupt from a deadline that is another context's.
	 */
	if (hart_has(HART_FEAT_SSTC))
		csr_write64(CSR_VSTIMECMP, ~ULL(0));
	for (unsigned int i = 0; i < last; i++)
		hyp_csr_write(i, ctx->hyp[i]);
	csr_write64(CSR_HTIMEDELTA, ctx->htimedelta);
	if (hart_has(HART_FEAT_MENVCFG))
		csr_write64(CSR_HENVCFG, ctx->henvcfg);
	if (hart_has(HART_FEAT_SMSTATEEN))
		csr_write64(CSR_HSTATEEN0, ctx->hstateen0);
	if (hart_has(HART_FEAT_SSTC))
		csr_write64(CSR_VSTIMECMP, ctx->vstimecmp);
	guest_tlb_flush();
#if __RISCV_XLEN__ == 32
	/*
	 * Every context there is to restore was left from HS-mode, or is
	 * new; the hart may have been taken out of a guest of a domain
	 * that is being stopped. On RV64 this is in the saved mstatus.
	 */
	csr_clear(CSR_MSTATUSH, MSTATUSH_MPV | MSTATUSH_GVA);
#endif
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
	/* Nobody's while the hart is between two contexts. */
	ctx->mpxy_shmem = mpxy_hart_shmem_swap(MPXY_SHMEM_NONE);
	hyp_state_save(ctx);
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
	hyp_state_restore(ctx);
	unit_state_restore(ctx);

	/* A deadline that has passed fires again at once. */
	timer_smode_restore(ctx->timer);
	csr_clear(mip, MIP_SSIP);
	if ((ctx->sip & MIP_SSIP) || ctx->ipi)
		csr_set(mip, MIP_SSIP);
	ctx->ipi = false;
	mpxy_hart_shmem_swap(ctx->mpxy_shmem);

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

static void context_park(struct domain_context *ctx, enum context_state state,
			 const struct trap_regs *regs, bool quiet)
{
	struct hart *h = this_hart();

	context_save(ctx, regs);
	ctx->state = state;
	ctx->quiet = quiet;
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
	struct vec_state *vec = ctx->vec;
	unsigned long mstatus = regs->mstatus;

	memset(ctx, 0, sizeof(*ctx));
	ctx->caller = caller;
	ctx->vec = vec;
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
	ctx->vstimecmp = ~ULL(0);
#if __RISCV_XLEN__ == 64
	/* As out of reset, but for the one field zero may not be a value of. */
	if (hart_has(HART_FEAT_H))
		ctx->hyp[HYP_HSTATUS] = csr_read(CSR_HSTATUS) & HSTATUS_VSXL;
#endif
	ctx->mpxy_shmem = MPXY_SHMEM_NONE;
	if (vec) {
		memset(vec, 0, sizeof(*vec) + 32 * vec_bytes);
		/* vill, as out of reset */
		vec->vtype = BIT(__RISCV_XLEN__ - 1);
	}
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
	} else if (!to->quiet) {
		to->regs.a0 = (unsigned long)error;
		to->regs.a1 = value;
	}
	to->quiet = false;
	context_restore(to, fresh);
	/* A message this context sent through the switch: what came of it. */
	if (!fresh)
		mpxy_context_resumed(&to->regs);
	if (regs)
		*regs = to->regs;
}

static bool caller_waits(const struct domain_context *ctx)
{
	return ctx->caller && ctx->caller->state == CONTEXT_CALLING;
}

static long enter(struct trap_regs *regs, struct domain *target,
		  unsigned long arg, bool quiet)
{
	unsigned int self = this_hart_index();
	struct domain_context *cur = NULL, *to = NULL;
	long rc = SBI_SUCCESS;

	spin_lock(&domain_lock);
	cur = context_of(this_domain(), self);
	to = context_of(target, self);
	if (target == this_domain() || !hartmask_test(&target->possible, self))
		rc = SBI_ERR_INVALID_PARAM;
	/*
	 * A service that moves the hart has its own say-so: the tree's, in its
	 * node.
	 */
	else if (!quiet && !domain_may_enter(this_domain(), target))
		rc = SBI_ERR_DENIED;
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

	context_park(cur, CONTEXT_CALLING, regs, quiet);
	to->caller = cur;
	context_switch(to, regs, SBI_SUCCESS, arg);
	spin_unlock(&domain_lock);
	return SBI_SUCCESS;
}

long domain_enter(struct trap_regs *regs, struct domain *target,
		  unsigned long arg)
{
	return enter(regs, target, arg, false);
}

/*
 * Only where the hart gets somewhere: a context that waits to be entered
 * again, or the domain's boot here. Any other hart would be stopped for
 * the domain to start it, and the caller with it.
 */
bool domain_enterable(const struct domain *target)
{
	unsigned int self = this_hart_index();
	enum context_state state = 0;

	if (target == this_domain() ||
	    !hartmask_test(&target->possible, self) ||
	    atomic_load_ulong(&target->stopping))
		return false;
	/* The hart's own contexts: nobody else moves them. */
	state = context_of(target, self)->state;
	return state == CONTEXT_IDLE ||
	       (state == CONTEXT_NONE && target->boot_hart == (int)self);
}

long domain_switch_to(struct trap_regs *regs, struct domain *target)
{
	return domain_enterable(target) ? enter(regs, target, 0, true) :
					  SBI_ERR_DENIED;
}

int domain_caller_key(void)
{
	const struct domain_context *cur =
		context_of(this_domain(), this_hart_index());

	return caller_waits(cur) ? (int)domain_of(cur->caller)->index : -1;
}

static long leave(struct trap_regs *regs, unsigned long value, bool quiet,
		  struct domain *first)
{
	unsigned int self = this_hart_index();
	struct domain_context *cur = NULL, *to = NULL;

	spin_lock(&domain_lock);
	cur = context_of(this_domain(), self);
	if (caller_waits(cur)) {
		to = cur->caller;
	} else if (quiet) {
		/*
		 * No one to go back to: 'first' gets its boot, if it is due one
		 * here.
		 */
		if (first && first != this_domain() &&
		    first->boot_hart == (int)self &&
		    hartmask_test(&first->possible, self) &&
		    !atomic_load_ulong(&first->stopping) &&
		    context_of(first, self)->state == CONTEXT_NONE &&
		    domain_range_ok(first, first->next_addr, 4,
				    DOMAIN_PERM_SU_X))
			to = context_of(first, self);
		if (to)
			to->caller = NULL;
	} else {
		/*
		 * Booting: a domain that has not had this hart yet. That is
		 * entering it, and takes its leave to; never the root domain,
		 * whose next stage and all of memory are not for a domain to
		 * bring together when it likes.
		 */
		for (unsigned int d = 1; d < domain_count() && !to; d++) {
			struct domain *dom = domain_by_index(d);

			if (dom != this_domain() &&
			    hartmask_test(&dom->possible, self) &&
			    domain_may_enter(this_domain(), dom) &&
			    !atomic_load_ulong(&dom->stopping) &&
			    context_of(dom, self)->state == CONTEXT_NONE)
				to = context_of(dom, self);
		}
		if (to)
			to->caller = NULL;
	}
	if (!to || atomic_load_ulong(&this_domain()->stopping)) {
		spin_unlock(&domain_lock);
		return SBI_ERR_DENIED;
	}

	cur->caller = NULL;
	context_park(cur, CONTEXT_IDLE, regs, quiet);
	context_switch(to, regs, SBI_SUCCESS, value);
	spin_unlock(&domain_lock);
	return SBI_SUCCESS;
}

long domain_exit(struct trap_regs *regs, unsigned long value)
{
	return leave(regs, value, false, NULL);
}

long domain_switch_back(struct trap_regs *regs, struct domain *first)
{
	return leave(regs, 0, true, first);
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
