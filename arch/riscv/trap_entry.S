/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * M-mode trap entry: save the full GPR file, call
 * trap_handler(struct trap_regs *), restore and mret.
 *
 * mscratch tells where the trap came from (see <arch/hart.h>):
 *   0         M-mode. The frame goes on the current stack and tp is kept.
 *   non-zero  a lower privilege level. mscratch is the hart's struct hart:
 *             it becomes tp, and the frame goes on top of the hart's M-mode
 *             stack, so the monitor never runs on a stack the trapping
 *             context controls.
 */

#include <arch/hart.h>
#include <arch/trap.h>
#include <util.h>

	.section .text.trap, "ax", %progbits
	.balign	4
	.globl _trap_entry
_trap_entry:
	csrrw	tp, mscratch, tp
	bnez	tp, 1f

	/* From M-mode: undo the swap, mscratch stays 0. */
	csrrw	tp, mscratch, tp
	addi	sp, sp, -TRAP_REGS_SIZE
	REG_S	t0, TRAP_REGS_t0(sp)
	addi	t0, sp, TRAP_REGS_SIZE
	REG_S	t0, TRAP_REGS_sp(sp)
	REG_S	tp, TRAP_REGS_tp(sp)
	j	2f

1:	/* From S/U-mode: tp = struct hart, mscratch = interrupted tp. */
	REG_S	sp, HART_TMP(tp)
	REG_L	sp, HART_M_SP(tp)
	addi	sp, sp, -TRAP_REGS_SIZE
	REG_S	t0, TRAP_REGS_t0(sp)
	REG_L	t0, HART_TMP(tp)
	REG_S	t0, TRAP_REGS_sp(sp)
	csrrw	t0, mscratch, zero
	REG_S	t0, TRAP_REGS_tp(sp)

2:	REG_S	ra, TRAP_REGS_ra(sp)
	REG_S	gp, TRAP_REGS_gp(sp)
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
	csrr	t0, mepc
	REG_S	t0, TRAP_REGS_mepc(sp)
	csrr	t0, mstatus
	REG_S	t0, TRAP_REGS_mstatus(sp)

	/*
	 * Smdbltrp: the hart set mstatus.MDT on the way in, and a trap taken
	 * with MDT set is a fatal double trap. The state is saved now, so
	 * traps are survivable again, which the expected M-mode traps (CSR
	 * probing, unprivileged accesses) rely on. mret clears MDT itself.
	 */
#if __RISCV_XLEN__ == 64
	li	t0, BIT(MSTATUS_MDT_SHIFT)
	csrc	mstatus, t0
#else
	li	t0, BIT(MSTATUSH_MDT_SHIFT)
	csrc	CSR_MSTATUSH, t0
#endif

.option push
.option norelax
	la	gp, __global_pointer$
.option pop

	mv	a0, sp
	call	trap_handler

_trap_return:
	/* Returning below M-mode: park the hart pointer in mscratch. */
	REG_L	t0, TRAP_REGS_mstatus(sp)
	csrw	mstatus, t0
	srli	t0, t0, MSTATUS_MPP_SHIFT
	andi	t0, t0, 3
	addi	t0, t0, -PRV_M
	beqz	t0, 3f
	csrw	mscratch, tp
3:
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

	/*
	 * _trap_frame_return(frame): leave M-mode the way a trap handler
	 * returns, with 'frame' for a register file, from anywhere. The frame
	 * goes where a trap would have put it; nothing else of the M-mode
	 * stack survives, so 'frame' is not to live there.
	 */
	.globl _trap_frame_return
_trap_frame_return:
	REG_L	sp, HART_M_SP(tp)
	addi	sp, sp, -TRAP_REGS_SIZE
	mv	t0, sp
	addi	t1, a0, TRAP_REGS_SIZE
1:	REG_L	t2, 0(a0)
	REG_S	t2, 0(t0)
	addi	a0, a0, REGBYTES
	addi	t0, t0, REGBYTES
	bne	a0, t1, 1b
	j	_trap_return
