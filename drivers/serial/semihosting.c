// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Semihosting: output through a debugger or a simulator that watches for
 * the magic instruction sequence, where there is no UART to be had. A last
 * resort, asked when no node and no other driver gave a console, and only
 * taken when someone answers: without a host the ebreak is a trap, which is
 * how that shows.
 *
 * Output only. Reading a character blocks until there is one, and the
 * monitor never waits for a keyboard.
 */

#ifdef IMAGE_MONITOR

#include <arch/hart.h>
#include <console.h>
#include <serial.h>

#define SYS_WRITEC 0x03
#define SYS_ERRNO 0x13

/* The three instructions have to be uncompressed and in one page. */
static long __noinline __aligned(16) semihost(long op, long arg)
{
	register long a0 __asm__("a0") = op;
	register long a1 __asm__("a1") = arg;

	__asm__ __volatile__(".option push\n.option norvc\n"
			     "slli zero, zero, 0x1f\n"
			     "ebreak\n"
			     "srai zero, zero, 7\n"
			     ".option pop"
			     : "+r"(a0)
			     : "r"(a1)
			     : "memory");
	return a0;
}

static void semihosting_putc(char c)
{
	semihost(SYS_WRITEC, (long)&c);
}

static int semihosting_getc(void)
{
	return -1;
}

static const struct console_ops semihosting_console = {
	.name = "semihosting",
	.putc = semihosting_putc,
	.getc = semihosting_getc,
};

static int semihosting_init(const struct serial_params *p)
{
	if (p || !may_trap(semihost(SYS_ERRNO, 0)))
		return -1;
	console_register(&semihosting_console);
	return 0;
}

SERIAL_DEFINE(semihosting) = {
	.name = "semihosting",
	.init = semihosting_init,
	.last_resort = true,
};

#endif
