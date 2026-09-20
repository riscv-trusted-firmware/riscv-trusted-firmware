/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Reset entry, shared by every image (docs/boot.md). Contract with the previous
 * stage (or the reset vector): a0 = hart id, a1 = device tree address, a2 = a
 * hand-over block (<handover.h>) or nothing of the kind.
 *
 * Boot hart: first hart to win the lottery, among those the hand-over block
 * allows to. It zeroes .bss, sets up its stack and calls
 * image_main(hartid, fdt, handover). Other harts wait for the boot hart to
 * release them, then call image_secondary_main(hartid) on their own stack.
 * _boot_hart_count is the number of harts that entered the image,
 * _boot_hart_parked how many of them the image does not manage (they are
 * not in the hart table of <boot.h>).
 */

#include <arch/csr.h>
#include <handover.h>

#define R_RISCV_RELATIVE	3

	.section .text.entry, "ax", %progbits
	.globl _start
_start:
	csrw	mie, zero

	/*
	 * s0 = the hand-over block, 0 without one; s1 = the hart that is to
	 * boot, -1 for any. a2 may be anything when the previous stage knows
	 * nothing of this: a trap while looking ends the look.
	 */
	mv	s0, zero
	li	s1, -1
#ifdef CONFIG_BOOT_HANDOVER
	la	t0, 1f
	csrw	mtvec, t0
	beqz	a2, 1f
	andi	t0, a2, REGBYTES - 1
	bnez	t0, 1f
	REG_L	t0, HANDOVER_MAGIC(a2)
	li	t1, BOOT_HANDOVER_MAGIC
	bne	t0, t1, 1f
	REG_L	t0, HANDOVER_VERSION(a2)
	mv	s0, a2
	li	t1, 2
	bltu	t0, t1, 1f
	REG_L	s1, HANDOVER_BOOT_HART(a2)
	.balign	4
1:
#endif
	/* Machine mode, interrupts off, clean state. */
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
	/*
	 * Not everybody has a ticket when the previous stage names the winner.
	 */
	li	t0, -1
	beq	s1, t0, 1f
	bne	s1, a0, _secondary
1:	la	t0, _boot_lottery
	li	t1, 1
	amoswap.w t2, t1, (t0)
	bnez	t2, _secondary
#else
	li	t0, -1
	beq	s1, t0, 1f
	bne	s1, a0, _secondary
	j	2f
1:	bnez	a0, _secondary
2:
#endif

#ifdef IMAGE_PIE
	/*
	 * Boot hart: the image may run somewhere else than it was linked for.
	 * Everything the code does is PC-relative; what needs fixing are the
	 * addresses stored in data (tables of pointers), which the linker left
	 * as R_RISCV_RELATIVE relocations: *(offset + delta) = addend + delta.
	 * The words start out as zero, so this runs even when delta is 0, and
	 * doing it again (a warm restart) changes nothing.
	 */
	la	t0, _start
	/* _start is the first thing in the image */
	li	t1, IMAGE_LINK_ADDR
	sub	t0, t0, t1			/* delta */
	la	t1, __rela_start
	la	t2, __rela_end
1:	bgeu	t1, t2, 3f
	REG_L	t3, REGBYTES(t1)		/* r_info */
	li	t4, R_RISCV_RELATIVE
	bne	t3, t4, 2f
	REG_L	t3, 0(t1)			/* r_offset */
	REG_L	t4, (2 * REGBYTES)(t1)		/* r_addend */
	add	t3, t3, t0
	add	t4, t4, t0
	REG_S	t4, 0(t3)
2:	addi	t1, t1, 3 * REGBYTES
	j	1b
3:
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
	mv	a2, s0
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
