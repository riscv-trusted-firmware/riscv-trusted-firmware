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

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>

struct trap_regs {
	unsigned long zero, ra, sp, gp, tp, t0, t1, t2, s0, s1;
	unsigned long a0, a1, a2, a3, a4, a5, a6, a7;
	unsigned long s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
	unsigned long t3, t4, t5, t6;
	unsigned long mepc;
	unsigned long mstatus;
};

/* x0..x31 by register number, for instruction emulation. */
static inline unsigned long *trap_reg(struct trap_regs *regs, unsigned int n)
{
	return &((unsigned long *)regs)[n];
}

/* Cause and auxiliary values of a trap, read once at handler entry. */
struct trap_info {
	unsigned long cause;
	unsigned long tval;
	unsigned long tval2; /* H extension */
	unsigned long tinst; /* H extension */
	bool gva; /* tval is a guest virtual address */
	bool virt; /* taken from VS/VU-mode */
};

void trap_handler(struct trap_regs *regs);

/* Print the register file and panic. */
void __noreturn trap_fatal(const struct trap_regs *regs, const char *what);

/* Runtime (monitor) only. */
/* Deliver 'info' to S-mode (HS-mode) as if it had been delegated. */
void trap_redirect(struct trap_regs *regs, const struct trap_info *info);
void trap_illegal_insn(struct trap_regs *regs, const struct trap_info *info);
/* Emulate a misaligned load or store, or hand it to S-mode. */
void trap_misaligned(struct trap_regs *regs, const struct trap_info *info);
/* Its vector half: false when 'insn' is not a vector access it emulates. */
bool trap_misaligned_vector(struct trap_regs *regs, unsigned long insn);

#endif /* !__ASSEMBLY__ */

#endif
