// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Device tree fix-ups for the next stage.
 *
 * The monitor's memory is fenced off by PMP, but it lies inside what the
 * device tree describes as RAM: without a /reserved-memory entry an
 * operating system hands those pages out and faults on first use. The
 * entry is "no-map" so that the memory does not end up in a linear
 * mapping either, where speculative accesses could reach it. The same goes
 * for an RPMI shared memory transport carved out of RAM.
 */

#include <arch/hart.h>
#include <libfdt.h>
#include <log.h>
#include <stdio.h>

#include "fdt_fixup.h"

/* Room for the nodes added below, with some to spare. */
#define FDT_FIXUP_ROOM 512

static int cells_of(const void *fdt, int node, const char *prop, int dflt)
{
	const fdt32_t *val = fdt_getprop(fdt, node, prop, NULL);

	return val ? (int)fdt32_to_cpu(*val) : dflt;
}

static void put_cells(fdt32_t **p, int cells, uint64_t val)
{
	for (int i = cells - 1; i >= 0; i--, val >>= 32)
		(*p)[i] = cpu_to_fdt32((uint32_t)val);
	*p += cells;
}

static int reserve(void *fdt, const char *what, uint64_t base, uint64_t size)
{
	int ac = cells_of(fdt, 0, "#address-cells", 2);
	int sc = cells_of(fdt, 0, "#size-cells", 1);
	fdt32_t reg[4] = {}, *p = reg;
	char name[32] = {};
	int parent = 0, node = 0, rc = 0;

	parent = fdt_path_offset(fdt, "/reserved-memory");
	if (parent < 0) {
		parent = fdt_add_subnode(fdt, 0, "reserved-memory");
		if (parent < 0)
			return parent;
		rc = fdt_setprop_u32(fdt, parent, "#address-cells",
				     (uint32_t)ac);
		if (!rc)
			rc = fdt_setprop_u32(fdt, parent, "#size-cells",
					     (uint32_t)sc);
		if (!rc)
			rc = fdt_setprop_empty(fdt, parent, "ranges");
		if (rc)
			return rc;
	} else {
		ac = cells_of(fdt, parent, "#address-cells", ac);
		sc = cells_of(fdt, parent, "#size-cells", sc);
	}
	if (ac < 1 || ac > 2 || sc < 1 || sc > 2)
		return -FDT_ERR_BADNCELLS;

	snprintf(name, sizeof(name), "%s@%llx", what, (unsigned long long)base);
	node = fdt_add_subnode(fdt, parent, name);
	if (node < 0)
		return node;
	put_cells(&p, ac, base);
	put_cells(&p, sc, size);
	rc = fdt_setprop(fdt, node, "reg", reg,
			 (ac + sc) * (int)sizeof(fdt32_t));
	if (!rc)
		rc = fdt_setprop_empty(fdt, node, "no-map");
	return rc;
}

unsigned long fdt_fixup(unsigned long fdt)
{
	unsigned long dst = CONFIG_MONITOR_FDT_ADDR ? CONFIG_MONITOR_FDT_ADDR :
						      fdt;
	unsigned long size = 0;
	int rc = 0;

	if (!fdt || fdt_check_header((void *)fdt)) {
		pr_warn("fdt: no valid device tree at %lx\n", fdt);
		return fdt;
	}

	/* The tree grows in place unless the configuration gives it a home. */
	size = fdt_totalsize((void *)fdt) + FDT_FIXUP_ROOM;
	if (!smode_range_ok(dst, size)) {
		pr_warn("fdt: %lx+%lx overlaps the monitor\n", dst, size);
		return fdt;
	}
	rc = fdt_open_into((void *)fdt, (void *)dst, (int)size);
	if (!rc)
		rc = reserve((void *)dst, "monitor", CONFIG_MONITOR_LOAD_ADDR,
			     CONFIG_MONITOR_SIZE);
#ifdef CONFIG_RPMI_SHMEM_IN_RAM
	if (!rc)
		rc = reserve((void *)dst, "rpmi-shmem", CONFIG_RPMI_SHMEM_BASE,
			     4 * CONFIG_RPMI_SHMEM_QUEUE_SIZE);
#endif
	if (rc) {
		pr_warn("fdt: fix-up failed: %s\n", fdt_strerror(rc));
		return fdt;
	}
	fdt_pack((void *)dst);
	return dst;
}
