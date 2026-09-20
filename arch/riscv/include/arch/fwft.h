/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_FWFT_H
#define ARCH_FWFT_H

/*
 * Firmware features: per-hart M-mode controls S-mode may change (SBI FWFT
 * semantics and error codes). A feature exists when the hart implements
 * what is behind it, which for the menvcfg ones is probed by writing.
 */

/* Every hart, before it enters the next stage: reset values, nothing locked. */
void fwft_hart_init(void);

long fwft_set(unsigned long feature, unsigned long value, unsigned long flags);
long fwft_get(unsigned long feature, unsigned long *value);

#endif
