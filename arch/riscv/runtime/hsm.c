// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/sse.h>
#include <atomic.h>
#include <domain.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <suspend.h>
#include <timer.h>
#include <util.h>

/* Serialises hart_start requests aimed at the same hart. */
static unsigned long hsm_start_lock = SPINLOCK_UNLOCK;

static const struct hsm_ops *hsm_ops;

void hsm_register(const struct hsm_ops *ops)
{
	hsm_ops = ops;
}

const char *hsm_name(void)
{
	return hsm_ops ? hsm_ops->name : "none";
}

static bool hsm_transition(struct hart *h, unsigned long from, unsigned long to)
{
	return atomic_cas_ulong(&h->hsm_state, &from, to);
}

static void __noreturn hsm_hart_enter(bool resume)
{
	struct hart *h = this_hart();

	/*
	 * A started hart gets a clean slate. A resumed one keeps its pending
	 * interrupts and timer: they are what woke it up.
	 */
	if (!resume)
		hart_runtime_init();
	/* The domain's context on this hart is this, from here on. */
	domain_context_started();
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STARTED);
	hart_enter_smode(h->start_addr, h->hartid, h->start_arg, h->start_mode);
}

void __noreturn hsm_boot_hart_start(unsigned long entry, unsigned long arg,
				    unsigned long mode)
{
	struct hart *h = this_hart();

	h->start_addr = entry;
	h->start_arg = arg;
	h->start_mode = mode;
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_START_PENDING);
	hsm_hart_enter(false);
}

static void hsm_wait_loop(void)
{
	struct hart *h = this_hart();

	/* Only an IPI wakes a stopped hart. */
	csr_write(mie, 0);
	ipi_hart_init();
	while (atomic_load_ulong(&h->hsm_state) !=
	       SBI_HSM_STATE_START_PENDING) {
		wfi();
		ipi_process();
	}
	hsm_hart_enter(false);
}

/*
 * A hart that lost its state in a non-retentive suspend the platform made
 * real comes back through the monitor's entry, like a started one: what it
 * resumes to is where a start would have put it.
 */
static void hsm_resume_from_reset(void)
{
	atomic_store_ulong(&this_hart()->hsm_state,
			   SBI_HSM_STATE_RESUME_PENDING);
	hsm_hart_enter(false);
}

void __noreturn hsm_hart_wait(void)
{
	struct hart *h = this_hart();

	if (atomic_load_ulong(&h->hsm_state) == SBI_HSM_STATE_SUSPENDED)
		hart_restart_stack(hsm_resume_from_reset);
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STOPPED);
	hart_restart_stack(hsm_wait_loop);
}

/* A hart stops itself: tell the platform, which may power it down in WFI. */
static void __noreturn hsm_hart_stopped(void)
{
	if (hsm_ops && hsm_ops->hart_stop)
		hsm_ops->hart_stop(this_hartid());
	hsm_hart_wait();
}

int hsm_hart_start(unsigned long hartid, unsigned long entry, unsigned long arg)
{
	struct hart *h = hart_get(hartid);

	/* A domain sees its own harts and no others. */
	if (!hart_valid(hartid) || !domain_hart_member(this_domain(), h->index))
		return SBI_ERR_INVALID_PARAM;
	/* One that is away in another domain runs, as far as this one goes. */
	if (!domain_hart_assigned(this_domain(), h->index))
		return SBI_ERR_ALREADY_AVAILABLE;
	if (!smode_entry_ok(entry))
		return SBI_ERR_INVALID_ADDRESS;
	return hsm_hart_boot(h->index, entry, arg, PRV_S);
}

int hsm_hart_boot(unsigned int index, unsigned long entry, unsigned long arg,
		  unsigned long mode)
{
	struct hart *h = hart_by_index(index);
	unsigned long hartid = hart_id_of(index);
	unsigned long state = 0;

	if (!hart_index_valid(index))
		return SBI_ERR_INVALID_PARAM;
	if (!ipi_available())
		return SBI_ERR_FAILED;

	/* The target reads the arguments once it sees START_PENDING. */
	spin_lock(&hsm_start_lock);
	state = atomic_load_ulong(&h->hsm_state);
	if (domain_stop_pending_on(index)) {
		spin_unlock(&hsm_start_lock);
		return SBI_ERR_DENIED;
	}
	if (state == SBI_HSM_STATE_STOPPED) {
		h->start_addr = entry;
		h->start_arg = arg;
		h->start_mode = mode;
		atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_START_PENDING);
	}
	spin_unlock(&hsm_start_lock);

	if (state == SBI_HSM_STATE_STARTED ||
	    state == SBI_HSM_STATE_START_PENDING)
		return SBI_ERR_ALREADY_AVAILABLE;
	if (state != SBI_HSM_STATE_STOPPED)
		return SBI_ERR_INVALID_STATE;

	/* The platform gets the hart running, the kick gets it out of WFI. */
	if (hsm_ops && hsm_ops->hart_start && hsm_ops->hart_start(hartid)) {
		hsm_transition(h, SBI_HSM_STATE_START_PENDING,
			       SBI_HSM_STATE_STOPPED);
		return SBI_ERR_FAILED;
	}
	ipi_kick(h->index);
	return SBI_SUCCESS;
}

void hsm_start_barrier(void)
{
	spin_lock(&hsm_start_lock);
	spin_unlock(&hsm_start_lock);
}

int hsm_hart_stop(void)
{
	struct hart *h = this_hart();

	if (!hsm_transition(h, SBI_HSM_STATE_STARTED,
			    SBI_HSM_STATE_STOP_PENDING))
		return SBI_ERR_FAILED;

	timer_hart_init();
	hsm_hart_stopped();
}

void __noreturn hsm_hart_force_stop(void)
{
	atomic_store_ulong(&this_hart()->hsm_state, SBI_HSM_STATE_STOP_PENDING);
	timer_hart_init();
	hsm_hart_stopped();
}

/* Sleep until an interrupt the next stage has enabled is pending. */
static void hsm_wait_for_wakeup(void)
{
	for (;;) {
		unsigned long pending = csr_read(mip) & csr_read(mie);

		if (pending & ipi_irq())
			ipi_process();
		if (pending & MIP_MTIP)
			timer_process();
		/* S-mode has work: an interrupt it enabled, or an event. */
		if ((csr_read(mip) & csr_read(mie) & csr_read(mideleg)) ||
		    sse_pending())
			break;
		/* The domain is being stopped: that happens on the way out. */
		if (domain_stop_pending())
			break;
		wfi();
	}
}

static void hsm_resume_loop(void)
{
	struct hart *h = this_hart();

	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_SUSPENDED);
	hsm_wait_for_wakeup();
	/*
	 * Nothing of S-mode is left to resume: stopping takes no more than
	 * this.
	 */
	if (domain_stop_pending())
		domain_stop_self(NULL);
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_RESUME_PENDING);
	hsm_hart_enter(true);
}

/* 'tell': a hart suspend of its own, not the last step of a system suspend. */
static int hart_suspend(unsigned long type, unsigned long resume_addr,
			unsigned long arg, bool tell)
{
	struct hart *h = this_hart();
	bool retentive = false, platform = false;
	long rc = 0;

	/*
	 * The default types, the platform's, and what is reserved in between.
	 */
	if (type > UL(0xffffffff) ||
	    (type > SBI_HSM_SUSPEND_RET_DEFAULT &&
	     type < SBI_HSM_SUSPEND_RET_PLATFORM) ||
	    (type > SBI_HSM_SUSPEND_NON_RET_DEFAULT &&
	     type < SBI_HSM_SUSPEND_NON_RET_PLATFORM))
		return SBI_ERR_INVALID_PARAM;
	retentive = type < SBI_HSM_SUSPEND_NON_RET_DEFAULT;
	platform = type != SBI_HSM_SUSPEND_RET_DEFAULT &&
		   type != SBI_HSM_SUSPEND_NON_RET_DEFAULT;
	if (platform && !(hsm_ops && hsm_ops->hart_suspend))
		return SBI_ERR_NOT_SUPPORTED;

	if (!retentive && !smode_entry_ok(resume_addr))
		return SBI_ERR_INVALID_ADDRESS;
	if (!hsm_transition(h, SBI_HSM_STATE_STARTED,
			    SBI_HSM_STATE_SUSPEND_PENDING))
		return SBI_ERR_FAILED;

	/*
	 * The platform's part: it may take the hart down once it sees it
	 * wait. Whatever it does, the wait below is what the hart does.
	 */
	rc = tell && hsm_ops && hsm_ops->hart_suspend ?
		     hsm_ops->hart_suspend(h->hartid, (uint32_t)type,
					   monitor_base()) :
		     SBI_SUCCESS;
	if (rc && platform) {
		atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STARTED);
		return (int)rc;
	}

	if (!retentive) {
		h->start_addr = resume_addr;
		h->start_arg = arg;
		h->start_mode = PRV_S;
		hart_restart_stack(hsm_resume_loop);
	}

	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_SUSPENDED);
	hsm_wait_for_wakeup();
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_RESUME_PENDING);
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STARTED);
	return SBI_SUCCESS;
}

int hsm_hart_suspend(unsigned long type, unsigned long resume_addr,
		     unsigned long arg)
{
	return hart_suspend(type, resume_addr, arg, true);
}

int hsm_system_suspend(uint32_t sleep_type, unsigned long resume_addr,
		       unsigned long arg)
{
	unsigned int self = this_hart_index();
	bool alone = true;
	long rc = 0;

	if (!domain_suspend_allowed(this_domain()))
		return SBI_ERR_DENIED;
	if (!smode_entry_ok(resume_addr))
		return SBI_ERR_INVALID_ADDRESS;
	/*
	 * Nobody can start a hart behind our back: we are the only one running,
	 * of the domain that is. What other domains do is not its business,
	 * but then the platform stays up.
	 */
	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
		if (i == self || !hart_index_valid(i) ||
		    atomic_load_ulong(&hart_by_index(i)->hsm_state) ==
			    SBI_HSM_STATE_STOPPED)
			continue;
		if (domain_hart_assigned(this_domain(), i))
			return SBI_ERR_DENIED;
		alone = false;
	}
	if (domain_has_parked(this_domain()))
		return SBI_ERR_DENIED;

	/* A platform that knows how to sleep is told; the rest is the same. */
	if (alone && suspend_supported(sleep_type)) {
		rc = suspend_prepare(sleep_type, resume_addr);
		if (rc)
			return (int)rc;
	}
	return hart_suspend(SBI_HSM_SUSPEND_NON_RET_DEFAULT, resume_addr, arg,
			    false);
}

long hsm_hart_state(unsigned long hartid)
{
	struct hart *h = hart_get(hartid);

	if (!hart_valid(hartid) || !domain_hart_member(this_domain(), h->index))
		return SBI_ERR_INVALID_PARAM;
	/* Away in another domain, to come back: it runs. */
	if (!domain_hart_assigned(this_domain(), h->index))
		return SBI_HSM_STATE_STARTED;
	return (long)atomic_load_ulong(&h->hsm_state);
}

void hsm_interruptible_mask(struct hartmask *mask)
{
	hartmask_clear_all(mask);
	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
		unsigned long state = 0;

		if (!hart_index_valid(i) ||
		    !domain_hart_assigned(this_domain(), i))
			continue;
		state = atomic_load_ulong(&hart_by_index(i)->hsm_state);
		if (state == SBI_HSM_STATE_STARTED ||
		    state == SBI_HSM_STATE_SUSPENDED)
			hartmask_set(mask, i);
	}
}
