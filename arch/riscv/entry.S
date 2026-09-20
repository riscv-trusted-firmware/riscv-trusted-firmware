/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Reset entry, shared by every image. Contract with the previous stage
 * (or the reset vector): a0 = hart id, a1 = device tree address.
 *
 * Boot hart: first hart to win the lottery. It zeroes .bss, sets up its
 * stack and calls image_main(hartid, fdt). Other harts wait for the boot hart
 * to release them, then call image_secondary_main(hartid) on their own stack.
 * _boot_hart_count is the number of harts that entered the image,
 * _boot_hart_parked how many of them the image does not manage (they are
 * not in the hart table of <boot.h>).
 */

#include <arch/csr.h>

	.section .text.entry, "ax", %progbits
	.globl _start
_start:
	/* Machine mode, interrupts off, clean state. */
	csrw	mie, zero
	csrw	mip, zero
	csrw	mstatus, zero
#if __RISCV_XLEN__ == 32
	/* with Smdbltrp, MDT is set out of reset */
	csrw	CSR_MSTATUSH, zero
#endif
	csrw	mscratch, zero
	mv	tp, zero

.option push
.option norelax
	la	gp, __global_pointer$
.option pop

	la	t0, _trap_entry
	csrw	mtvec, t0

#ifdef CONFIG_RISCV_ISA_A
	/* Count the harts that showed up, then draw the boot hart. */
	la	t0, _boot_hart_count
	li	t1, 1
	amoadd.w t2, t1, (t0)
	la	t0, _boot_lottery
	li	t1, 1
	amoswap.w t2, t1, (t0)
	bnez	t2, _secondary
#else
	bnez	a0, _secondary
#endif

	/* Boot hart: clear .bss before touching any C state. */
	la	t0, __bss_start
	la	t1, __bss_end
1:	bgeu	t0, t1, 2f
	REG_S	zero, 0(t0)
	addi	t0, t0, REGBYTES
	j	1b
2:
	/* Whatever its id, the boot hart is hart index 0. */
	mv	a2, zero
	call	_setup_stack
	fence	rw, rw
	call	image_main
	j	_park

_secondary:
	/* Wait for the boot hart to finish initialising shared state. */
	la	t0, _boot_release
1:	lw	t1, 0(t0)
	fence	r, rw
	beqz	t1, 1b

	/*
	 * The boot hart has filled _boot_hart_ids[] by now: a hart's index is
	 * its id's position there. Hart ids can be anything; an index is
	 * below CONFIG_PLATFORM_HART_COUNT and has a stack.
	 */
	la	t0, _boot_hart_ids
	la	t1, _boot_hart_nr
	lw	t1, 0(t1)
	mv	a2, zero
1:	bgeu	a2, t1, 3f
	REG_L	t2, 0(t0)
	beq	t2, a0, 2f
	addi	t0, t0, REGBYTES
	addi	a2, a2, 1
	j	1b
2:	call	_setup_stack
	call	image_secondary_main
	j	_park

3:	/* Not a hart the image manages: say so, and stay out of the way. */
#ifdef CONFIG_RISCV_ISA_A
	la	t0, _boot_hart_parked
	li	t1, 1
	amoadd.w t2, t1, (t0)
#endif
	j	_park

	/* sp = top of the stack of hart index a2. Clobbers t0-t2. */
_setup_stack:
	li	t0, CONFIG_STACK_SIZE
	mv	t1, zero
	mv	t2, a2
1:	beqz	t2, 2f		/* t1 = index * STACK_SIZE without M */
	add	t1, t1, t0
	addi	t2, t2, -1
	j	1b
2:	la	sp, __stack_top
	sub	sp, sp, t1
	ret

	.globl _park
_park:
	wfi
	j	_park

	.section .data
	.balign	4
_boot_lottery:
	.word	0
	.globl _boot_release
_boot_release:
	.word	0
	.globl _boot_hart_count
_boot_hart_count:
	.word	0
	.globl _boot_hart_parked
_boot_hart_parked:
	.word	0
