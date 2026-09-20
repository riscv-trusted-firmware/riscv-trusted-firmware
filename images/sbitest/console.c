// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* printf() backend of the test payload: the SBI debug console. */

#include <console.h>

#include "sbicall.h"

void console_putc(char c)
{
	if (c == '\n')
		sbi_call1(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE, '\r');
	sbi_call1(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE, (unsigned char)c);
}

void console_puts(const char *s)
{
	while (*s)
		console_putc(*s++);
}
