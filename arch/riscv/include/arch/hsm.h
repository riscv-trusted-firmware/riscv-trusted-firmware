/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_HSM_H
#define ARCH_HSM_H

/*
 * Hart state management: which harts run the next stage, and how they get
 * there and back. States and error codes are the SBI HSM ones.
 */

#include <arch/trap.h>
#include <hartmask.h>

/*
 * Optional platform control over hart power, around the state machine: a
 * stopped hart sits in WFI in the monitor either way, and a platform may
 * take it down from there and bring it back through the monitor's entry
 * point. Return 0 or an SBI error code.
 */
struct hsm_ops {
	const char *name;
	/*
	 * Make sure 'hartid' runs (again); called by the hart that starts it.
	 */
	long (*hart_start)(unsigned long hartid);
	/* The calling hart is about to wait until it is started again. */
	void (*hart_stop)(unsigned long hartid);
	/*
	 * The calling hart is about to wait in the suspend state 'type', an
	 * SBI HSM suspend type, platform specific ones included: the default
	 * ones work without anybody's help, so for them an answer other than
	 * SBI_SUCCESS is not an error. SBI_ERR_NOT_SUPPORTED for a type the
	 * platform does not have. 'resume_addr' is where a hart that lost its
	 * state comes back: the monitor's entry, not S-mode's address.
	 */
	long (*hart_suspend)(unsigned long hartid, uint32_t type,
			     unsigned long resume_addr);
};

void hsm_register(const struct hsm_ops *ops);
const char *hsm_name(void);

/* Boot hart: becomes STARTED and enters the next stage at 'entry' in 'mode'. */
void __noreturn hsm_boot_hart_start(unsigned long entry, unsigned long arg,
				    unsigned long mode);

/* Calling hart is STOPPED: wait in M-mode until another hart starts it. */
void __noreturn hsm_hart_wait(void);

/*
 * On behalf of S-mode: a hart of the caller's domain, at an address of its own.
 */
int hsm_hart_start(unsigned long hartid, unsigned long entry,
		   unsigned long arg);
/* The monitor's own: any stopped hart, PRV_S or PRV_U, no questions asked. */
int hsm_hart_boot(unsigned int index, unsigned long entry, unsigned long arg,
		  unsigned long mode);
/* No hsm_hart_boot() that began before this returns is still under way. */
void hsm_start_barrier(void);
/* Does not return on success. */
int hsm_hart_stop(void);
/* The same, not asked for by S-mode and whatever the hart was doing. */
void __noreturn hsm_hart_force_stop(void);
/* A non-retentive suspend does not return on success. */
/*
 * Is 'type' a suspend type sbi_hart_suspend() takes here? SBI_SUCCESS,
 * SBI_ERR_INVALID_PARAM for a reserved one, SBI_ERR_NOT_SUPPORTED for one of
 * the platform's without a platform that has any.
 */
long hsm_suspend_type_check(unsigned long type);
int hsm_hart_suspend(unsigned long type, unsigned long resume_addr,
		     unsigned long arg);
/*
 * Non-retentive suspend of the calling hart as the last one running:
 * SBI_ERR_DENIED unless every other hart is stopped.
 */
int hsm_system_suspend(uint32_t sleep_type, unsigned long resume_addr,
		       unsigned long arg);
/* SBI_HSM_STATE_* or SBI_ERR_INVALID_PARAM. */
long hsm_hart_state(unsigned long hartid);

/*
 * Harts that can take an interrupt from the next stage: STARTED or SUSPENDED,
 * and of the caller's domain.
 */
void hsm_interruptible_mask(struct hartmask *mask);
/* The same of any domain (<domain.h>). */
struct domain;
void hsm_interruptible_mask_of(const struct domain *dom, struct hartmask *mask);

#endif
