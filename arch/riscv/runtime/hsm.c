// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/sse.h>
#include <atomic.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <suspend.h>
#include <timer.h>

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
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STARTED);
	hart_enter_smode(h->start_addr, h->hartid, h->start_arg);
}

void __noreturn hsm_boot_hart_start(unsigned long entry, unsigned long arg)
{
	struct hart *h = this_hart();

	h->start_addr = entry;
	h->start_arg = arg;
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

void __noreturn hsm_hart_wait(void)
{
	atomic_store_ulong(&this_hart()->hsm_state, SBI_HSM_STATE_STOPPED);
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
	unsigned long state = 0;

	if (!hart_valid(hartid))
		return SBI_ERR_INVALID_PARAM;
	if (!smode_range_ok(entry, 4))
		return SBI_ERR_INVALID_ADDRESS;
	if (!ipi_available())
		return SBI_ERR_FAILED;

	/* The target reads the arguments once it sees START_PENDING. */
	spin_lock(&hsm_start_lock);
	state = atomic_load_ulong(&h->hsm_state);
	if (state == SBI_HSM_STATE_STOPPED) {
		h->start_addr = entry;
		h->start_arg = arg;
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

int hsm_hart_stop(void)
{
	struct hart *h = this_hart();

	if (!hsm_transition(h, SBI_HSM_STATE_STARTED,
			    SBI_HSM_STATE_STOP_PENDING))
		return SBI_ERR_FAILED;

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
		wfi();
	}
}

static void hsm_resume_loop(void)
{
	struct hart *h = this_hart();

	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_SUSPENDED);
	hsm_wait_for_wakeup();
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_RESUME_PENDING);
	hsm_hart_enter(true);
}

int hsm_hart_suspend(unsigned long type, unsigned long resume_addr,
		     unsigned long arg)
{
	struct hart *h = this_hart();
	bool retentive = false;

	switch (type) {
	case SBI_HSM_SUSPEND_RET_DEFAULT:
		retentive = true;
		break;
	case SBI_HSM_SUSPEND_NON_RET_DEFAULT:
		retentive = false;
		break;
	default:
		/* Platform-specific types: none yet. The rest is reserved. */
		if ((type >= SBI_HSM_SUSPEND_RET_PLATFORM &&
		     type < SBI_HSM_SUSPEND_NON_RET_DEFAULT) ||
		    type >= SBI_HSM_SUSPEND_NON_RET_PLATFORM)
			return SBI_ERR_NOT_SUPPORTED;
		return SBI_ERR_INVALID_PARAM;
	}

	if (!retentive && !smode_range_ok(resume_addr, 4))
		return SBI_ERR_INVALID_ADDRESS;
	if (!hsm_transition(h, SBI_HSM_STATE_STARTED,
			    SBI_HSM_STATE_SUSPEND_PENDING))
		return SBI_ERR_FAILED;

	if (!retentive) {
		h->start_addr = resume_addr;
		h->start_arg = arg;
		hart_restart_stack(hsm_resume_loop);
	}

	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_SUSPENDED);
	hsm_wait_for_wakeup();
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_RESUME_PENDING);
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STARTED);
	return SBI_SUCCESS;
}

int hsm_system_suspend(uint32_t sleep_type, unsigned long resume_addr,
		       unsigned long arg)
{
	unsigned int self = this_hart_index();
	long rc = 0;

	if (!smode_range_ok(resume_addr, 4))
		return SBI_ERR_INVALID_ADDRESS;
	/*
	 * Nobody can start a hart behind our back: we are the only one running.
	 */
	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++)
		if (i != self && hart_index_valid(i) &&
		    atomic_load_ulong(&hart_by_index(i)->hsm_state) !=
			    SBI_HSM_STATE_STOPPED)
			return SBI_ERR_DENIED;

	/* A platform that knows how to sleep is told; the rest is the same. */
	if (suspend_supported(sleep_type)) {
		rc = suspend_prepare(sleep_type, resume_addr);
		if (rc)
			return (int)rc;
	}
	return hsm_hart_suspend(SBI_HSM_SUSPEND_NON_RET_DEFAULT, resume_addr,
				arg);
}

long hsm_hart_state(unsigned long hartid)
{
	if (!hart_valid(hartid))
		return SBI_ERR_INVALID_PARAM;
	return (long)atomic_load_ulong(&hart_get(hartid)->hsm_state);
}

void hsm_interruptible_mask(struct hartmask *mask)
{
	hartmask_clear_all(mask);
	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
		unsigned long state = 0;

		if (!hart_index_valid(i))
			continue;
		state = atomic_load_ulong(&hart_by_index(i)->hsm_state);
		if (state == SBI_HSM_STATE_STARTED ||
		    state == SBI_HSM_STATE_SUSPENDED)
			hartmask_set(mask, i);
	}
}
