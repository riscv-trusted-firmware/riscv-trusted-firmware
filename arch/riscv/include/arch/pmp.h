/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_PMP_H
#define ARCH_PMP_H

/* Program PMP entry 'idx' as a NAPOT region; perm is PMP_R | PMP_W | PMP_X. */
int pmp_set_napot(unsigned int idx, unsigned long base, unsigned long size,
		  unsigned int perm);
/* Program PMP entry 'idx' to match the whole address space. */
void pmp_set_all(unsigned int idx, unsigned int perm);

#endif
