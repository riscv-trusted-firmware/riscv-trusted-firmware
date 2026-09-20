/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The floating-point and vector state of a hart, to and from memory, for
 * the domain context switch. The monitor is built without F, D and V: the
 * instructions are enabled here alone. The caller has turned mstatus.FS or
 * mstatus.VS on.
 */

#include <arch/hart.h>
#include <util.h>

	.section .text.domain_state, "ax", %progbits

	.option push
	.option arch, +d

	/* Slots of 8 bytes whatever FLEN is; fcsr in slot 32. */
	.macro	fp_regs op
	.irp	n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	\op	f\n, (8 * \n)(a0)
	.endr
	.irp	n, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	\op	f\n, (8 * \n)(a0)
	.endr
	.endm

	/* _fp_state_save(buf, is_double), _fp_state_restore(buf, is_double) */
	.globl _fp_state_save
_fp_state_save:
	frcsr	t0
	REG_S	t0, (8 * 32)(a0)
	beqz	a1, 1f
	fp_regs	fsd
	ret
1:	fp_regs	fsw
	ret

	.globl _fp_state_restore
_fp_state_restore:
	REG_L	t0, (8 * 32)(a0)
	fscsr	t0
	beqz	a1, 1f
	fp_regs	fld
	ret
1:	fp_regs	flw
	ret

	.option pop

	.option push
	.option arch, +v

	/* v0..v31 as VLENB bytes each, whatever vtype says. */
	.macro	vec_regs op
	csrr	t1, vlenb
	vsetvli	t0, zero, e8, m1, ta, ma
	.irp	n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	\op	v\n, (a0)
	add	a0, a0, t1
	.endr
	.irp	n, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	\op	v\n, (a0)
	add	a0, a0, t1
	.endr
	.endm

	/*
	 * _vec_state_save(state), _vec_state_restore(state):
	 * state = { vstart, vcsr, vl, vtype, registers }.
	 */
	.globl _vec_state_save
_vec_state_save:
	csrr	t0, vstart
	REG_S	t0, (0 * REGBYTES)(a0)
	csrr	t0, vcsr
	REG_S	t0, (1 * REGBYTES)(a0)
	csrr	t0, vl
	REG_S	t0, (2 * REGBYTES)(a0)
	csrr	t0, vtype
	REG_S	t0, (3 * REGBYTES)(a0)
	addi	a0, a0, 4 * REGBYTES
	vec_regs vse8.v
	ret

	/*
	 * _vec_state_clear(): the state out of reset, more or less. For a
	 * register file too large to keep.
	 */
	.globl _vec_state_clear
_vec_state_clear:
	vsetvli	t0, zero, e8, m1, ta, ma
	.irp	n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	vmv.v.i	v\n, 0
	.endr
	.irp	n, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	vmv.v.i	v\n, 0
	.endr
	li	t0, BIT(__RISCV_XLEN__ - 1)	/* vill */
	vsetvl	zero, zero, t0
	csrw	vcsr, zero
	ret

	.globl _vec_state_restore
_vec_state_restore:
	mv	t2, a0
	addi	a0, a0, 4 * REGBYTES
	vec_regs vle8.v
	/* vl is within what vtype allows: it comes out as it went in. */
	REG_L	t0, (2 * REGBYTES)(t2)
	REG_L	t1, (3 * REGBYTES)(t2)
	vsetvl	zero, t0, t1
	REG_L	t0, (0 * REGBYTES)(t2)
	csrw	vstart, t0
	REG_L	t0, (1 * REGBYTES)(t2)
	csrw	vcsr, t0
	ret

	.option pop
