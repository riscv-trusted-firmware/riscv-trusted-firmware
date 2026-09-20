// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/csr.h>
#include <log.h>
#include <stdarg.h>

bool log_quiet;

void __noreturn panic(const char *fmt, ...)
{
	va_list ap;

	printf("PANIC: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	csr_write(mie, 0);
	for (;;)
		wfi();
}
