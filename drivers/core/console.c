// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Console multiplexer: a single active backend, registered by the platform's
 * early init. Output before registration is dropped.
 */

#include <console.h>
#include <stddef.h>

static const struct console_ops *console;

void console_register(const struct console_ops *ops)
{
	console = ops;
}

void console_putc(char c)
{
	if (!console)
		return;
	if (c == '\n')
		console->putc('\r');
	console->putc(c);
}

void console_puts(const char *s)
{
	while (*s)
		console_putc(*s++);
}

int console_getc(void)
{
	return (console && console->getc) ? console->getc() : -1;
}
