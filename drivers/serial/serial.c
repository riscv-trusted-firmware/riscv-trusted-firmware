// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* Finding the console's UART, see <serial.h>. */

#include <fdt_util.h>
#include <libfdt.h>
#include <linker_table.h>
#include <memregion.h>
#include <serial.h>
#include <string.h>

#define for_each_serial(d) LINKER_TABLE_FOREACH(d, serial)

static const struct serial_driver *driver_of(const void *fdt, int node)
{
	const struct serial_driver *d = NULL;

	for_each_serial(d)
		if (d->compatible &&
		    fdt_node_compatible_any(fdt, node, d->compatible))
			return d;
	return NULL;
}

static int init_from_node(const void *fdt, int node, uint32_t baud)
{
	const struct serial_driver *d = driver_of(fdt, node);
	struct serial_params p = { 0 };
	uint64_t base = 0, size = 0;

	if (!d || !fdt_node_enabled(fdt, node))
		return -1;
	if (!fdt_reg(fdt, node, 0, &base, &size)) {
		p.base = (uintptr_t)base;
		p.size = (unsigned long)size;
	}
	p.reg_shift = fdt_prop_u32(fdt, node, "reg-shift", 0);
	p.reg_width = fdt_prop_u32(fdt, node, "reg-io-width", 0);
	p.clock = fdt_prop_u32(fdt, node, "clock-frequency", 0);
	p.baud = baud ? baud : fdt_prop_u32(fdt, node, "current-speed", 0);
	if (d->init(&p))
		return -1;
	/* The next stage usually drives the same UART. */
	if (p.size)
		memregion_add(p.base, p.size, MEMREGION_SHARED_RW);
	return 0;
}

static uint32_t decimal(const char *s)
{
	uint32_t val = 0;

	while (*s >= '0' && *s <= '9')
		val = 10 * val + (uint32_t)(*s++ - '0');
	return val;
}

/*
 * /chosen/stdout-path: a path or an alias, optionally followed by ":115200n8".
 */
static int init_from_stdout_path(const void *fdt)
{
	int chosen = fdt_path_offset(fdt, "/chosen"), node = 0, len = 0,
	    namelen = 0;
	const char *path = NULL, *colon = NULL;

	path = chosen < 0 ? NULL :
			    fdt_getprop(fdt, chosen, "stdout-path", &len);
	if (!path)
		return -1;
	colon = strchr(path, ':');
	namelen = colon ? (int)(colon - path) : (int)strlen(path);
	node = fdt_path_offset_namelen(fdt, path, namelen);
	if (node < 0)
		return -1;
	return init_from_node(fdt, node, colon ? decimal(colon + 1) : 0);
}

void serial_console_init(const void *fdt)
{
	const struct serial_driver *d = NULL;
	int node = 0;

	fdt = fdt_valid(fdt);
	if (fdt && !init_from_stdout_path(fdt))
		return;
	if (fdt)
		for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
		     node = fdt_next_node(fdt, node, NULL))
			if (!init_from_node(fdt, node, 0))
				return;
	for (int last = 0; last <= 1; last++)
		for_each_serial(d)
			if (d->last_resort == last && !d->init(NULL))
				return;
}
