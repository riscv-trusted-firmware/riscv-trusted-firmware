/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Entry points of the domains next to the test payload's own (domain.c).
 * They run this image read-only, from memory of their own: no gp, no
 * .data, no .bss, a stack at the top of their memory.
 */

#include <arch/csr.h>
#include <sbi/sbi.h>

#define DOM_MEM		CONFIG_QEMU_VIRT_DOMAINS_MEM
#define DOM_MEM_SIZE	0x100000
#define DOM_STACK_SIZE	0x4000

	.section .text, "ax", %progbits

	/* sp = top of the memory at a2, less a stack per hart id (a0). */
	.macro	dom_stack
	li	t0, DOM_MEM_SIZE
	add	sp, a2, t0
	li	t0, DOM_STACK_SIZE
	mv	t1, a0
1:	beqz	t1, 2f
	sub	sp, sp, t0
	addi	t1, t1, -1
	j	1b
2:
	.endm

	.globl _trusted_start
_trusted_start:
	li	a2, DOM_MEM
	dom_stack
	la	t0, _trusted_trap
	csrw	stvec, t0
	call	trusted_main
1:	wfi
	j	1b

	.globl _trusted_secondary_start
_trusted_secondary_start:
	li	a2, DOM_MEM
	dom_stack
	la	t0, _trusted_trap
	csrw	stvec, t0
	call	trusted_secondary_main
1:	wfi
	j	1b

	.globl _island_start
_island_start:
	li	a2, DOM_MEM + DOM_MEM_SIZE
	dom_stack
	la	t0, _trusted_trap
	csrw	stvec, t0
	call	island_main
1:	wfi
	j	1b

	/* Skip the (4-byte) instruction and leave the cause in sscratch. */
	.balign	4
_trusted_trap:
	csrr	t6, sepc
	addi	t6, t6, 4
	csrw	sepc, t6
	csrr	t6, scause
	csrw	sscratch, t6
	sret

	/*
	 * long trusted_probe(addr, write): the cause of the fault, 0 for none.
	 */
	.globl trusted_probe
trusted_probe:
	csrw	sscratch, zero
.option push
.option norvc
	beqz	a1, 1f
	sb	zero, 0(a0)
	j	2f
1:	lb	t0, 0(a0)
.option pop
2:	csrr	a0, sscratch
	ret

	/*
	 * domain_enter_regs(domain, arg, out): SBI_FW_DOMAIN_ENTER with the
	 * callee-saved registers loaded with a pattern; what they hold on
	 * return goes to out[0..11], the call's (error, value) to out[12..13].
	 */
	.globl domain_enter_regs
domain_enter_regs:
	addi	sp, sp, -14 * REGBYTES
	REG_S	s0, 0 * REGBYTES(sp)
	REG_S	s1, 1 * REGBYTES(sp)
	REG_S	s2, 2 * REGBYTES(sp)
	REG_S	s3, 3 * REGBYTES(sp)
	REG_S	s4, 4 * REGBYTES(sp)
	REG_S	s5, 5 * REGBYTES(sp)
	REG_S	s6, 6 * REGBYTES(sp)
	REG_S	s7, 7 * REGBYTES(sp)
	REG_S	s8, 8 * REGBYTES(sp)
	REG_S	s9, 9 * REGBYTES(sp)
	REG_S	s10, 10 * REGBYTES(sp)
	REG_S	s11, 11 * REGBYTES(sp)
	REG_S	ra, 12 * REGBYTES(sp)
	REG_S	a2, 13 * REGBYTES(sp)
	li	s0, 0x500
	li	s1, 0x501
	li	s2, 0x502
	li	s3, 0x503
	li	s4, 0x504
	li	s5, 0x505
	li	s6, 0x506
	li	s7, 0x507
	li	s8, 0x508
	li	s9, 0x509
	li	s10, 0x50a
	li	s11, 0x50b
	li	a7, SBI_EXT_FW_DOMAIN
	li	a6, SBI_FW_DOMAIN_ENTER
	ecall
	REG_L	t0, 13 * REGBYTES(sp)
	REG_S	s0, 0 * REGBYTES(t0)
	REG_S	s1, 1 * REGBYTES(t0)
	REG_S	s2, 2 * REGBYTES(t0)
	REG_S	s3, 3 * REGBYTES(t0)
	REG_S	s4, 4 * REGBYTES(t0)
	REG_S	s5, 5 * REGBYTES(t0)
	REG_S	s6, 6 * REGBYTES(t0)
	REG_S	s7, 7 * REGBYTES(t0)
	REG_S	s8, 8 * REGBYTES(t0)
	REG_S	s9, 9 * REGBYTES(t0)
	REG_S	s10, 10 * REGBYTES(t0)
	REG_S	s11, 11 * REGBYTES(t0)
	REG_S	a0, 12 * REGBYTES(t0)
	REG_S	a1, 13 * REGBYTES(t0)
	REG_L	s0, 0 * REGBYTES(sp)
	REG_L	s1, 1 * REGBYTES(sp)
	REG_L	s2, 2 * REGBYTES(sp)
	REG_L	s3, 3 * REGBYTES(sp)
	REG_L	s4, 4 * REGBYTES(sp)
	REG_L	s5, 5 * REGBYTES(sp)
	REG_L	s6, 6 * REGBYTES(sp)
	REG_L	s7, 7 * REGBYTES(sp)
	REG_L	s8, 8 * REGBYTES(sp)
	REG_L	s9, 9 * REGBYTES(sp)
	REG_L	s10, 10 * REGBYTES(sp)
	REG_L	s11, 11 * REGBYTES(sp)
	REG_L	ra, 12 * REGBYTES(sp)
	addi	sp, sp, 14 * REGBYTES
	ret

	/*
	 * Floating-point and vector registers, for code built without either.
	 */
.option push
.option arch, +f, +v
	/*
	 * trusted_unit_probe(vector): the cause of the trap that using the
	 * unit gives, 0 when it is there. sstatus alone does not tell.
	 */
	.globl trusted_unit_probe
trusted_unit_probe:
	csrw	sscratch, zero
	beqz	a0, 1f
	vsetvli	t0, zero, e32, m1, ta, ma
	j	2f
1:	fmv.x.w	t0, f8
2:	csrr	a0, sscratch
	ret
	/* fp_set(x): f8 = x, f9 = ~x.  fp_get(): f8 ^ ~f9, x when intact. */
	.globl fp_set
fp_set:
	fmv.w.x	f8, a0
	not	a0, a0
	fmv.w.x	f9, a0
	ret
	.globl fp_get
fp_get:
	fmv.x.w	a0, f8
	ret
	.globl fp_get_inv
fp_get_inv:
	fmv.x.w	a0, f9
	not	a0, a0
	ret
	/* vec_set(x): every element of v8 = x, of v31 = ~x. */
	.globl vec_set
vec_set:
	vsetvli	t0, zero, e32, m1, ta, ma
	vmv.v.x	v8, a0
	not	a0, a0
	vmv.v.x	v31, a0
	ret
	/* Element 0 of v8 before anything was put there. */
	.globl vec_peek
vec_peek:
	vsetvli	t0, zero, e32, m1, ta, ma
	.globl vec_get
vec_get:
	vmv.x.s	a0, v8
	ret
	.globl vec_get_inv
vec_get_inv:
	vmv.x.s	a0, v31
	not	a0, a0
	ret
	/*
	 * The vector length in force: a set-up another domain made is not ours.
	 */
	.globl vec_vl
vec_vl:
	csrr	a0, vl
	ret
.option pop
