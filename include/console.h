/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef CONSOLE_H
#define CONSOLE_H

struct console_ops {
	const char *name;
	void (*putc)(char c);
	int (*getc)(void); /* -1 when nothing is pending */
};

void console_register(const struct console_ops *ops);
void console_putc(char c);
void console_puts(const char *s);
int console_getc(void);

#endif
