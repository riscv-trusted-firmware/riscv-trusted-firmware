/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_TRAP_H
#define ARCH_TRAP_H

#include <arch/csr.h>

/* Offsets into struct trap_regs, shared with trap_entry.S. */
#define TRAP_REGS_zero (0 * REGBYTES)
#define TRAP_REGS_ra (1 * REGBYTES)
#define TRAP_REGS_sp (2 * REGBYTES)
#define TRAP_REGS_gp (3 * REGBYTES)
#define TRAP_REGS_tp (4 * REGBYTES)
#define TRAP_REGS_t0 (5 * REGBYTES)
#define TRAP_REGS_t1 (6 * REGBYTES)
#define TRAP_REGS_t2 (7 * REGBYTES)
#define TRAP_REGS_s0 (8 * REGBYTES)
#define TRAP_REGS_s1 (9 * REGBYTES)
#define TRAP_REGS_a0 (10 * REGBYTES)
#define TRAP_REGS_a1 (11 * REGBYTES)
#define TRAP_REGS_a2 (12 * REGBYTES)
#define TRAP_REGS_a3 (13 * REGBYTES)
#define TRAP_REGS_a4 (14 * REGBYTES)
#define TRAP_REGS_a5 (15 * REGBYTES)
#define TRAP_REGS_a6 (16 * REGBYTES)
#define TRAP_REGS_a7 (17 * REGBYTES)
#define TRAP_REGS_s2 (18 * REGBYTES)
#define TRAP_REGS_s3 (19 * REGBYTES)
#define TRAP_REGS_s4 (20 * REGBYTES)
#define TRAP_REGS_s5 (21 * REGBYTES)
#define TRAP_REGS_s6 (22 * REGBYTES)
#define TRAP_REGS_s7 (23 * REGBYTES)
#define TRAP_REGS_s8 (24 * REGBYTES)
#define TRAP_REGS_s9 (25 * REGBYTES)
#define TRAP_REGS_s10 (26 * REGBYTES)
#define TRAP_REGS_s11 (27 * REGBYTES)
#define TRAP_REGS_t3 (28 * REGBYTES)
#define TRAP_REGS_t4 (29 * REGBYTES)
#define TRAP_REGS_t5 (30 * REGBYTES)
#define TRAP_REGS_t6 (31 * REGBYTES)
#define TRAP_REGS_mepc (32 * REGBYTES)
#define TRAP_REGS_mstatus (33 * REGBYTES)
#define TRAP_REGS_SIZE (34 * REGBYTES)

#ifndef __ASSEMBLY__

#include <stdint.h>

struct trap_regs {
	unsigned long zero, ra, sp, gp, tp, t0, t1, t2, s0, s1;
	unsigned long a0, a1, a2, a3, a4, a5, a6, a7;
	unsigned long s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
	unsigned long t3, t4, t5, t6;
	unsigned long mepc;
	unsigned long mstatus;
};

void trap_handler(struct trap_regs *regs);

#endif /* !__ASSEMBLY__ */

#endif
