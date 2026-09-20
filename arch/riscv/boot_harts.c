// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* The hart table, see <boot.h>. */

#include <boot.h>
#include <fdt_util.h>
#include <string.h>

unsigned long _boot_hart_ids[CONFIG_PLATFORM_HART_COUNT];
uint32_t _boot_hart_nr;

static void add_hart(unsigned long hartid)
{
	if (_boot_hart_nr < CONFIG_PLATFORM_HART_COUNT &&
	    boot_hart_index(hartid) < 0)
		_boot_hart_ids[_boot_hart_nr++] = hartid;
}

void boot_harts_init(const void *fdt, unsigned long boot_hartid)
{
	int cpus = fdt ? fdt_path_offset(fdt, "/cpus") : -1, cpu = 0;
	uint64_t hartid = 0;

	_boot_hart_nr = 0;
	add_hart(boot_hartid);

	if (cpus < 0) {
		/*
		 * Nothing to go by: harts 0 .. CONFIG_PLATFORM_HART_COUNT - 1.
		 */
		for (unsigned long id = 0; id < CONFIG_PLATFORM_HART_COUNT;
		     id++)
			add_hart(id);
		return;
	}
	fdt_for_each_subnode(cpu, fdt, cpus) {
		const char *type = fdt_getprop(fdt, cpu, "device_type", NULL);

		if (type && !strcmp(type, "cpu") &&
		    fdt_node_enabled(fdt, cpu) &&
		    !fdt_reg(fdt, cpu, 0, &hartid, NULL))
			add_hart((unsigned long)hartid);
	}
}

int boot_hart_index(unsigned long hartid)
{
	for (uint32_t i = 0; i < _boot_hart_nr; i++)
		if (_boot_hart_ids[i] == hartid)
			return (int)i;
	return -1;
}
