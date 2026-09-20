/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SERIAL_H
#define SERIAL_H

/*
 * The console's UART. It is needed before anything else, so these are not
 * table drivers probed with the rest: the platform's early init calls
 * serial_console_init(), which finds the driver of the node that
 * /chosen/stdout-path names, or failing that of the first node any driver
 * knows, and without a tree asks the drivers for a default of their own
 * (Kconfig geometry, a debugger's semihosting). A driver is a descriptor in
 * the .serial_table linker set; its init() registers a console (<console.h>).
 */

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * What the node says, as far as serial ports agree on it; 0 where it does not.
 */
struct serial_params {
	uintptr_t base;
	unsigned long size;
	unsigned int reg_shift;
	unsigned int reg_width; /* bytes */
	uint32_t clock; /* Hz */
	uint32_t baud;
};

struct serial_driver {
	const char *name;
	const char *const *compatible;
	/*
	 * 0 when the console is registered. 'p' is NULL when there is no node
	 * to go by: a driver without a default of its own declines (-1).
	 */
	int (*init)(const struct serial_params *p);
	/* Asked for a default after the drivers of real UARTs. */
	bool last_resort;
};

#define SERIAL_DEFINE(_sym) \
	static const struct serial_driver _sym __used __section(".serial_table")

extern const struct serial_driver __serial_table_start[];
extern const struct serial_driver __serial_table_end[];

/* 'fdt' may be NULL or no tree at all. */
void serial_console_init(const void *fdt);

#endif
