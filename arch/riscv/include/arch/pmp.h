/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_PMP_H
#define ARCH_PMP_H

#include <stdbool.h>
#include <types_ext.h>

/* mseccfg (Smepmp) */
#define CSR_MSECCFG 0x747
#define MSECCFG_MML 0x1
#define MSECCFG_MMWP 0x2
#define MSECCFG_RLB 0x4

/* Raw access to PMP entry 'idx': pmpaddr as the CSR wants it, cfg = PMP_*. */
void pmp_entry_set(unsigned int idx, unsigned long pmpaddr, unsigned int cfg);
void pmp_entry_cfg(unsigned int idx, unsigned int cfg);

/*
 * Cover [base, base + size) from entry 'idx' on: one NAPOT entry when the
 * range allows it, else an OFF entry holding the base and a TOR entry.
 * Returns the number of entries used.
 */
unsigned int pmp_range_set(unsigned int idx, paddr_t base, paddr_size_t size,
			   unsigned int perm);
/* Entry 'idx' matches the whole address space. */
void pmp_all_set(unsigned int idx, unsigned int perm);

/* Program the calling hart's PMP from the memory regions (hart.c). */
void pmp_hart_init(void);
/*
 * What comes after the monitor's own entries: the regions of the domain the
 * calling hart runs, smallest first (<domain.h>). Again whenever the hart
 * changes domain.
 */
void pmp_domain_set(void);
/* Entries left for a domain's regions. */
unsigned int pmp_domain_entries(void);

/*
 * M-mode is about to touch S-mode memory directly (a buffer S-mode named
 * by physical address). Under Smepmp that needs a rule of its own, open
 * between begin and end; without it these do nothing. Not nestable.
 */
void *smode_access_begin(paddr_t addr, paddr_size_t size);
void smode_access_end(void);

#endif
