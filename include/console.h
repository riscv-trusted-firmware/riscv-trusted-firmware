/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>

struct console_ops {
	const char *name;
	void (*putc)(char c);
	int (*getc)(void); /* -1 when nothing is pending */
};

void console_register(const struct console_ops *ops);
/* Text output: '\n' goes out as "\r\n". */
void console_putc(char c);
void console_puts(const char *s);
/* Raw bytes, untranslated. */
void console_write(const char *buf, size_t len);
/* Non-blocking: -1 / a short count when nothing (more) is pending. */
int console_getc(void);
size_t console_read(char *buf, size_t len);

#endif
