/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * S-mode entry points of the SBI test payload. All of them are entered
 * the SBI way: a0 = hart id, a1 = opaque (the device tree for _start),
 * satp = 0, interrupts off.
 *
 *   _start            boot hart, from the monitor
 * _start + 4, 8, 12 the other domains of a build that has them
 * (tdomain_entry.S)
 *   _secondary_start  sbi_hart_start() target
 *   _resume_start     non-retentive sbi_hart_suspend() resume address
 */

#include <arch/csr.h>

/* 16 caller-saved registers. */
#define FRAME_SIZE	(16 * REGBYTES)

	.section .text.entry, "ax", %progbits
	.globl _start
_start:
#ifdef CONFIG_QEMU_VIRT_DOMAINS
	/* Where the platform's device tree says the domains begin. */
.option push
.option norvc
	j	1f
	j	_trusted_start
	j	_island_start
	j	_trusted_secondary_start
.option pop
1:
#endif
	call	_setup
	la	t0, __bss_start
	la	t1, __bss_end
1:	bgeu	t0, t1, 2f
	REG_S	zero, 0(t0)
	addi	t0, t0, REGBYTES
	j	1b
2:	call	test_main
3:	wfi
	j	3b

	.section .text, "ax", %progbits
	.globl _secondary_start
_secondary_start:
	call	_setup
	call	secondary_main
1:	wfi
	j	1b

	.globl _resume_start
_resume_start:
	call	_setup
	call	resume_main
1:	wfi
	j	1b

	/* gp, stvec and this hart's stack (a0 = hart id). Clobbers t0-t2. */
_setup:
.option push
.option norelax
	la	gp, __global_pointer$
.option pop
	la	t0, _strap_entry
	csrw	stvec, t0
	/*
	 * sp = end of _sbitest_stacks[hart id]; ids we have no stack for wait.
	 */
	li	t0, 16				/* SBITEST_MAX_HARTS */
3:	bltu	a0, t0, 4f
	wfi
	j	3b
4:	li	t0, CONFIG_STACK_SIZE
	mv	t1, t0
	mv	t2, a0
1:	beqz	t2, 2f
	add	t1, t1, t0
	addi	t2, t2, -1
	j	1b
2:	la	sp, _sbitest_stacks
	add	sp, sp, t1
	ret

	/* sepc = strap_handler(scause, sepc, stval), on the current stack. */
	.balign	4
_strap_entry:
	addi	sp, sp, -FRAME_SIZE
	REG_S	ra, 0 * REGBYTES(sp)
	REG_S	t0, 1 * REGBYTES(sp)
	REG_S	t1, 2 * REGBYTES(sp)
	REG_S	t2, 3 * REGBYTES(sp)
	REG_S	a0, 4 * REGBYTES(sp)
	REG_S	a1, 5 * REGBYTES(sp)
	REG_S	a2, 6 * REGBYTES(sp)
	REG_S	a3, 7 * REGBYTES(sp)
	REG_S	a4, 8 * REGBYTES(sp)
	REG_S	a5, 9 * REGBYTES(sp)
	REG_S	a6, 10 * REGBYTES(sp)
	REG_S	a7, 11 * REGBYTES(sp)
	REG_S	t3, 12 * REGBYTES(sp)
	REG_S	t4, 13 * REGBYTES(sp)
	REG_S	t5, 14 * REGBYTES(sp)
	REG_S	t6, 15 * REGBYTES(sp)
	csrr	a0, scause
	csrr	a1, sepc
	csrr	a2, stval
	call	strap_handler
	csrw	sepc, a0
	REG_L	ra, 0 * REGBYTES(sp)
	REG_L	t0, 1 * REGBYTES(sp)
	REG_L	t1, 2 * REGBYTES(sp)
	REG_L	t2, 3 * REGBYTES(sp)
	REG_L	a0, 4 * REGBYTES(sp)
	REG_L	a1, 5 * REGBYTES(sp)
	REG_L	a2, 6 * REGBYTES(sp)
	REG_L	a3, 7 * REGBYTES(sp)
	REG_L	a4, 8 * REGBYTES(sp)
	REG_L	a5, 9 * REGBYTES(sp)
	REG_L	a6, 10 * REGBYTES(sp)
	REG_L	a7, 11 * REGBYTES(sp)
	REG_L	t3, 12 * REGBYTES(sp)
	REG_L	t4, 13 * REGBYTES(sp)
	REG_L	t5, 14 * REGBYTES(sp)
	REG_L	t6, 15 * REGBYTES(sp)
	addi	sp, sp, FRAME_SIZE
	sret

	/*
	 * SSE handler entry: a6 = hart id, a7 = the event's entry argument, a
	 * struct sse_ctx (sse.c). Everything else belongs to the interrupted
	 * code, the stack included, so the handler brings its own; a6 and
	 * a7 are put back by the SBI implementation on completion.
	 */
#define SSE_CTX_STACK	(0 * REGBYTES)
#define SSE_CTX_TMP	(1 * REGBYTES)
#define SSE_FRAME_SIZE	(18 * REGBYTES)

	.globl _sse_entry
	.balign	4
_sse_entry:
	REG_S	sp, SSE_CTX_TMP(a7)
	REG_L	sp, SSE_CTX_STACK(a7)
	addi	sp, sp, -SSE_FRAME_SIZE
	REG_S	ra, 0 * REGBYTES(sp)
	REG_S	t0, 1 * REGBYTES(sp)
	REG_S	t1, 2 * REGBYTES(sp)
	REG_S	t2, 3 * REGBYTES(sp)
	REG_S	a0, 4 * REGBYTES(sp)
	REG_S	a1, 5 * REGBYTES(sp)
	REG_S	a2, 6 * REGBYTES(sp)
	REG_S	a3, 7 * REGBYTES(sp)
	REG_S	a4, 8 * REGBYTES(sp)
	REG_S	a5, 9 * REGBYTES(sp)
	REG_S	t3, 10 * REGBYTES(sp)
	REG_S	t4, 11 * REGBYTES(sp)
	REG_S	t5, 12 * REGBYTES(sp)
	REG_S	t6, 13 * REGBYTES(sp)
	REG_L	t0, SSE_CTX_TMP(a7)
	REG_S	t0, 14 * REGBYTES(sp)		/* interrupted sp */
	mv	a0, a7
	mv	a1, a6
	call	sse_handler
	REG_L	ra, 0 * REGBYTES(sp)
	REG_L	t0, 1 * REGBYTES(sp)
	REG_L	t1, 2 * REGBYTES(sp)
	REG_L	t2, 3 * REGBYTES(sp)
	REG_L	a0, 4 * REGBYTES(sp)
	REG_L	a1, 5 * REGBYTES(sp)
	REG_L	a2, 6 * REGBYTES(sp)
	REG_L	a3, 7 * REGBYTES(sp)
	REG_L	a4, 8 * REGBYTES(sp)
	REG_L	a5, 9 * REGBYTES(sp)
	REG_L	t3, 10 * REGBYTES(sp)
	REG_L	t4, 11 * REGBYTES(sp)
	REG_L	t5, 12 * REGBYTES(sp)
	REG_L	t6, 13 * REGBYTES(sp)
	REG_L	sp, 14 * REGBYTES(sp)
	li	a7, 0x535345			/* SBI_EXT_SSE */
	li	a6, 6				/* sbi_sse_complete */
	ecall
	/* Not reached: completion resumes the interrupted code. */
1:	j	1b

	/*
	 * Breakpoint target of the debug trigger tests: a 4-byte first
	 * instruction.
	 */
	.globl dbtr_target
	.balign	4
dbtr_target:
.option push
.option norvc
	nop
.option pop
	ret

	/*
	 * long dbtr_icount_run(ext, fid): install the one trigger of the shared
	 * memory and run into a sled of nops, one of which an instruction count
	 * trigger gets to skip.
	 */
	.globl dbtr_icount_run
	.balign	4
dbtr_icount_run:
.option push
.option norvc
	mv	a7, a0
	mv	a6, a1
	li	a0, 1
	ecall
	.rept	16
	nop
	.endr
.option pop
	ret
