/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef DRIVER_H
#define DRIVER_H

/*
 * Drivers are static descriptors in the .driver_table linker set. The core
 * probes them in table order on the boot hart. A driver takes its
 * configuration from Kconfig or looks its device up in the device tree.
 */

#include <compiler.h>
#include <linker_table.h>

struct driver {
	const char *name;
	/*
	 * 'fdt' is the device tree of the previous stage, NULL when there is
	 * none.
	 */
	int (*probe)(const void *fdt);
};

#define DRIVER_DEFINE(_sym) \
	static const struct driver _sym __used __section(".driver_table")

extern const struct driver __driver_table_start[];
extern const struct driver __driver_table_end[];

#define for_each_driver(d) LINKER_TABLE_FOREACH(d, driver)

void drivers_init(const void *fdt);

#endif
