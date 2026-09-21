// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/csr.h>
#ifdef IMAGE_MONITOR
#include <arch/hart.h>
#include <ipi.h>
#endif
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

#ifdef IMAGE_MONITOR
	/*
	 * A monitor that has found itself wrong is wrong for every hart: the
	 * others stop too, rather than run S-mode on top of it, or wait in
	 * M-mode for a lock or an answer that this hart was to give.
	 */
	for (unsigned int i = 0; hart_by_index(i); i++)
		if (!this_hart() || i != this_hart_index())
			ipi_send(i, IPI_EVENT_HALT);
#endif
	csr_write(mie, 0);
	for (;;)
		wfi();
}
