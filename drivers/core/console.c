// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Console multiplexer: a single active backend, registered by the platform's
 * early init. Output before registration is dropped.
 */

#include <console.h>
#include <spinlock.h>
#include <stddef.h>

static const struct console_ops *console;
/* Keeps the output of one call in one piece when several harts print. */
static unsigned long console_lock = SPINLOCK_UNLOCK;

void console_register(const struct console_ops *ops)
{
	console = ops;
}

static void putc_cooked(char c)
{
	if (c == '\n')
		console->putc('\r');
	console->putc(c);
}

void console_putc(char c)
{
	if (!console)
		return;
	spin_lock(&console_lock);
	putc_cooked(c);
	spin_unlock(&console_lock);
}

void console_puts(const char *s)
{
	if (!console)
		return;
	spin_lock(&console_lock);
	while (*s)
		putc_cooked(*s++);
	spin_unlock(&console_lock);
}

void console_write(const char *buf, size_t len)
{
	if (!console)
		return;
	spin_lock(&console_lock);
	while (len--)
		console->putc(*buf++);
	spin_unlock(&console_lock);
}

int console_getc(void)
{
	int c = 0;

	if (!console || !console->getc)
		return -1;
	spin_lock(&console_lock);
	c = console->getc();
	spin_unlock(&console_lock);
	return c;
}

size_t console_read(char *buf, size_t len)
{
	size_t n = 0;

	if (!console || !console->getc)
		return 0;
	spin_lock(&console_lock);
	while (n < len) {
		int c = console->getc();

		if (c < 0)
			break;
		buf[n++] = (char)c;
	}
	spin_unlock(&console_lock);
	return n;
}
