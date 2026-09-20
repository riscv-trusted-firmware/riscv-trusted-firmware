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

/* Boot hart: becomes STARTED and enters the next stage at 'entry'. */
void __noreturn hsm_boot_hart_start(unsigned long entry, unsigned long arg);

/* Calling hart is STOPPED: wait in M-mode until another hart starts it. */
void __noreturn hsm_hart_wait(void);

int hsm_hart_start(unsigned long hartid, unsigned long entry,
		   unsigned long arg);
/* Does not return on success. */
int hsm_hart_stop(void);
/* A non-retentive suspend does not return on success. */
int hsm_hart_suspend(unsigned long type, unsigned long resume_addr,
		     unsigned long arg);
/*
 * Non-retentive suspend of the calling hart as the last one running:
 * SBI_ERR_DENIED unless every other hart is stopped.
 */
int hsm_system_suspend(unsigned long resume_addr, unsigned long arg);
/* SBI_HSM_STATE_* or SBI_ERR_INVALID_PARAM. */
long hsm_hart_state(unsigned long hartid);

/*
 * Harts that can take an interrupt from the next stage: STARTED or SUSPENDED.
 */
void hsm_interruptible_mask(struct hartmask *mask);

#endif
