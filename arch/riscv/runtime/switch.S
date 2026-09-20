/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Leaving M-mode and restarting the M-mode stack. See <arch/hart.h> for the
 * tp / mscratch convention.
 */

#include <arch/hart.h>

	.section .text.switch, "ax", %progbits

	/*
	 * _hart_mret(entry, arg0, arg1): mret to 'entry' with a0 = arg0,
	 * a1 = arg1. The caller has programmed mstatus.MPP; nothing of the
	 * monitor's register state is left visible to the next stage.
	 */
	.globl _hart_mret
_hart_mret:
	csrw	mepc, a0
	csrw	mscratch, tp
	mv	a0, a1
	mv	a1, a2
	mv	a2, zero
	mv	a3, zero
	mv	a4, zero
	mv	a5, zero
	mv	a6, zero
	mv	a7, zero
	mv	t0, zero
	mv	t1, zero
	mv	t2, zero
	mv	t3, zero
	mv	t4, zero
	mv	t5, zero
	mv	t6, zero
	mv	s0, zero
	mv	s1, zero
	mv	s2, zero
	mv	s3, zero
	mv	s4, zero
	mv	s5, zero
	mv	s6, zero
	mv	s7, zero
	mv	s8, zero
	mv	s9, zero
	mv	s10, zero
	mv	s11, zero
	mv	ra, zero
	mv	sp, zero
	mv	gp, zero
	mv	tp, zero
	mret

	/*
	 * hart_restart_stack(fn): sp = top of the M-mode stack, tail-call fn().
	 */
	.globl hart_restart_stack
hart_restart_stack:
	REG_L	sp, HART_M_SP(tp)
	mv	ra, zero
	jr	a0
