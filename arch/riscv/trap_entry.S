/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * M-mode trap entry: save the full GPR file on the trapping hart's stack,
 * call trap_handler(struct trap_regs *), restore and mret.
 *
 * Traps taken from M-mode itself reuse the current stack; traps from lower
 * privilege levels land on the same per-hart firmware stack (mscratch is not
 * used yet: the firmware stack pointer is recovered from the hart id later,
 * when per-hart scratch areas are introduced).
 */

#include <arch/trap.h>

	.section .text.trap, "ax", %progbits
	.balign	4
	.globl _trap_entry
_trap_entry:
	addi	sp, sp, -TRAP_REGS_SIZE
	REG_S	ra, TRAP_REGS_ra(sp)
	REG_S	gp, TRAP_REGS_gp(sp)
	REG_S	tp, TRAP_REGS_tp(sp)
	REG_S	t0, TRAP_REGS_t0(sp)
	REG_S	t1, TRAP_REGS_t1(sp)
	REG_S	t2, TRAP_REGS_t2(sp)
	REG_S	s0, TRAP_REGS_s0(sp)
	REG_S	s1, TRAP_REGS_s1(sp)
	REG_S	a0, TRAP_REGS_a0(sp)
	REG_S	a1, TRAP_REGS_a1(sp)
	REG_S	a2, TRAP_REGS_a2(sp)
	REG_S	a3, TRAP_REGS_a3(sp)
	REG_S	a4, TRAP_REGS_a4(sp)
	REG_S	a5, TRAP_REGS_a5(sp)
	REG_S	a6, TRAP_REGS_a6(sp)
	REG_S	a7, TRAP_REGS_a7(sp)
	REG_S	s2, TRAP_REGS_s2(sp)
	REG_S	s3, TRAP_REGS_s3(sp)
	REG_S	s4, TRAP_REGS_s4(sp)
	REG_S	s5, TRAP_REGS_s5(sp)
	REG_S	s6, TRAP_REGS_s6(sp)
	REG_S	s7, TRAP_REGS_s7(sp)
	REG_S	s8, TRAP_REGS_s8(sp)
	REG_S	s9, TRAP_REGS_s9(sp)
	REG_S	s10, TRAP_REGS_s10(sp)
	REG_S	s11, TRAP_REGS_s11(sp)
	REG_S	t3, TRAP_REGS_t3(sp)
	REG_S	t4, TRAP_REGS_t4(sp)
	REG_S	t5, TRAP_REGS_t5(sp)
	REG_S	t6, TRAP_REGS_t6(sp)
	addi	t0, sp, TRAP_REGS_SIZE
	REG_S	t0, TRAP_REGS_sp(sp)
	csrr	t0, mepc
	REG_S	t0, TRAP_REGS_mepc(sp)
	csrr	t0, mstatus
	REG_S	t0, TRAP_REGS_mstatus(sp)

	mv	a0, sp
	call	trap_handler

	REG_L	t0, TRAP_REGS_mstatus(sp)
	csrw	mstatus, t0
	REG_L	t0, TRAP_REGS_mepc(sp)
	csrw	mepc, t0
	REG_L	ra, TRAP_REGS_ra(sp)
	REG_L	gp, TRAP_REGS_gp(sp)
	REG_L	tp, TRAP_REGS_tp(sp)
	REG_L	t0, TRAP_REGS_t0(sp)
	REG_L	t1, TRAP_REGS_t1(sp)
	REG_L	t2, TRAP_REGS_t2(sp)
	REG_L	s0, TRAP_REGS_s0(sp)
	REG_L	s1, TRAP_REGS_s1(sp)
	REG_L	a0, TRAP_REGS_a0(sp)
	REG_L	a1, TRAP_REGS_a1(sp)
	REG_L	a2, TRAP_REGS_a2(sp)
	REG_L	a3, TRAP_REGS_a3(sp)
	REG_L	a4, TRAP_REGS_a4(sp)
	REG_L	a5, TRAP_REGS_a5(sp)
	REG_L	a6, TRAP_REGS_a6(sp)
	REG_L	a7, TRAP_REGS_a7(sp)
	REG_L	s2, TRAP_REGS_s2(sp)
	REG_L	s3, TRAP_REGS_s3(sp)
	REG_L	s4, TRAP_REGS_s4(sp)
	REG_L	s5, TRAP_REGS_s5(sp)
	REG_L	s6, TRAP_REGS_s6(sp)
	REG_L	s7, TRAP_REGS_s7(sp)
	REG_L	s8, TRAP_REGS_s8(sp)
	REG_L	s9, TRAP_REGS_s9(sp)
	REG_L	s10, TRAP_REGS_s10(sp)
	REG_L	s11, TRAP_REGS_s11(sp)
	REG_L	t3, TRAP_REGS_t3(sp)
	REG_L	t4, TRAP_REGS_t4(sp)
	REG_L	t5, TRAP_REGS_t5(sp)
	REG_L	t6, TRAP_REGS_t6(sp)
	REG_L	sp, TRAP_REGS_sp(sp)
	mret
