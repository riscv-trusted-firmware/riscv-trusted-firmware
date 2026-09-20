// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <driver.h>
#include <fdt_util.h>
#include <log.h>
#include <util.h>

static bool driver_matches(const struct driver *d, const void *fdt, int node)
{
	return d->compatible &&
	       fdt_node_compatible_any(fdt, node, d->compatible);
}

static void driver_probe(const struct driver *d, const void *fdt, int node)
{
	int rc = d->probe ? d->probe(fdt, node) : 0;

	if (rc)
		pr_warn("driver %s: probe failed (%d)\n", d->name, rc);
	else
		pr_dbg("driver %s: ok\n", d->name);
}

void drivers_init(const void *fdt)
{
	const struct driver *d = NULL;
	int node = 0;

	for (unsigned int stage = 0; stage < DRIVER_STAGES; stage++) {
		/* One bit per driver of the table: some node matched it. */
		unsigned long long matched = 0;

		if (fdt)
			for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
			     node = fdt_next_node(fdt, node, NULL)) {
				if (!fdt_node_enabled(fdt, node))
					continue;
				for_each_driver(d) {
					if (d->stage != stage ||
					    !driver_matches(d, fdt, node))
						continue;
					matched |=
						BIT64(d - __driver_table_start);
					driver_probe(d, fdt, node);
				}
			}

		for_each_driver(d) {
			if (d->stage != stage)
				continue;
			if (!d->compatible ||
			    (d->probe_without_node &&
			     !(matched & BIT64(d - __driver_table_start))))
				driver_probe(d, fdt, -1);
		}
	}
}

int drivers_fdt_fixup(void *fdt)
{
	const struct driver *d = NULL;
	int node = 0, rc = 0;

	for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
	     node = fdt_next_node(fdt, node, NULL))
		for_each_driver(d) {
			if (!d->mmode_only || !driver_matches(d, fdt, node))
				continue;
			rc = fdt_node_disable(fdt, node);
			if (rc)
				return rc;
		}
	return 0;
}
