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
 * _boot_hart_count is the number of harts that entered the image.
 */

#include <arch/csr.h>

	.section .text.entry, "ax", %progbits
	.globl _start
_start:
	/* Machine mode, interrupts off, clean state. */
	csrw	mie, zero
	csrw	mip, zero
	csrw	mstatus, zero
	csrw	mscratch, zero
	mv	tp, zero

.option push
.option norelax
	la	gp, __global_pointer$
.option pop

	la	t0, _trap_entry
	csrw	mtvec, t0

	/* Harts beyond the configured maximum have no stack: park them. */
	li	t0, CONFIG_PLATFORM_HART_COUNT
	bgeu	a0, t0, _park

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
	call	_setup_stack
	call	image_secondary_main
	j	_park

	/* sp = top of this hart's stack (a0 = hart id). Clobbers t0-t2. */
_setup_stack:
	li	t0, CONFIG_STACK_SIZE
	mv	t1, zero
	mv	t2, a0
1:	beqz	t2, 2f		/* t1 = hartid * STACK_SIZE without M */
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
