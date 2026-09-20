/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef DRIVER_H
#define DRIVER_H

/*
 * Drivers are static descriptors in the .driver_table linker set, probed on
 * the boot hart. A driver names the device tree nodes it handles by their
 * "compatible" strings and is probed once for every enabled node that
 * matches; what it needs to know (addresses, geometry, which hart sits
 * where) it reads from the node.
 *
 * Probing goes by stage, so that a driver finds what it depends on already
 * there: an RPMI transport before the drivers that send messages over it.
 */

#include <compiler.h>
#include <linker_table.h>
#include <stdbool.h>

#define DRIVER_STAGE_EARLY 0 /* stand-alone devices, transports */
#define DRIVER_STAGE_LATE 1 /* users of other drivers' devices */
#define DRIVER_STAGES 2

struct driver {
	const char *name;
	/* NULL-terminated. NULL: not a device; probed once, with node = -1. */
	const char *const *compatible;
	unsigned int stage;
	/*
	 * The driver has a built-in configuration (Kconfig) to fall back on:
	 * probe it with node = -1 when no node of the tree matched, or when
	 * there is no tree.
	 */
	bool probe_without_node;
	/*
	 * The matching nodes describe something that is the monitor's alone:
	 * they are disabled in the device tree of the next stage.
	 */
	bool mmode_only;
	int (*probe)(const void *fdt, int node);
};

#define DRIVER_DEFINE(_sym) \
	static const struct driver _sym __used __section(".driver_table")

extern const struct driver __driver_table_start[];
extern const struct driver __driver_table_end[];

#define for_each_driver(d) LINKER_TABLE_FOREACH(d, driver)

/* 'fdt' is the device tree of the previous stage, NULL when there is none. */
void drivers_init(const void *fdt);
/* Disable the nodes of mmode_only drivers. 0 or a libfdt error. */
int drivers_fdt_fixup(void *fdt);

#endif
