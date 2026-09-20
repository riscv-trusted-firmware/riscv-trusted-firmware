/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_DBTR_H
#define ARCH_DBTR_H

/*
 * Debug triggers for S-mode (SBI DBTR) on top of the Sdtrig CSRs, which
 * only M-mode can reach. Semantics and error codes are those of the SBI
 * specification. Address / data match triggers (tdata1 types 2 and 6) are
 * supported; a trigger index is the index of the hardware trigger.
 */

#include <stdbool.h>

#ifdef CONFIG_SBI_DBTR

/* Every hart, before it enters the next stage: find the triggers, all free. */
/* Boot hart, once the domains are known. */
void dbtr_init(void);
void dbtr_hart_init(void);
/* The calling hart changes domain, see <arch/hart.h>. */
void dbtr_hart_switch_out(void);
void dbtr_hart_switch_in(bool fresh);

unsigned long dbtr_num_triggers(unsigned long tdata1);
long dbtr_set_shmem(unsigned long lo, unsigned long hi, unsigned long flags);
long dbtr_read(unsigned long base, unsigned long count);
/* *failed: index of the configuration that could not be taken. */
long dbtr_install(unsigned long count, unsigned long *failed);
long dbtr_update(unsigned long count, unsigned long *failed);
long dbtr_uninstall(unsigned long base, unsigned long mask);
long dbtr_enable(unsigned long base, unsigned long mask);
long dbtr_disable(unsigned long base, unsigned long mask);

#else

static inline void dbtr_init(void)
{
}

static inline void dbtr_hart_init(void)
{
}

static inline void dbtr_hart_switch_out(void)
{
}

static inline void dbtr_hart_switch_in(bool fresh)
{
}

#endif

#endif
