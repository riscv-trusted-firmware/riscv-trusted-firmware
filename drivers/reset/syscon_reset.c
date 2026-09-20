// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Generic "syscon-poweroff" and "syscon-reboot": a value written to a
 * register of a system controller ("regmap", "offset", "value", and an
 * optional "mask" of the bits to change). Device tree only.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <memregion.h>
#include <reset.h>
#include <stdint.h>
#include <types_ext.h>
#include <util.h>

struct syscon_action {
	vaddr_t reg; /* 0: not there */
	uint32_t value, mask;
};

/* [0]: power off, [1]: reboot (cold and warm alike). */
static struct syscon_action actions[2];

static struct syscon_action *action_for(enum reset_type type)
{
	return &actions[type == RESET_SHUTDOWN ? 0 : 1];
}

static bool syscon_reset_supported(enum reset_type type)
{
	return action_for(type)->reg != 0;
}

static void syscon_reset_write(enum reset_type type)
{
	const struct syscon_action *a = action_for(type);

	io_mask32(a->reg, a->value, a->mask);
}

static const struct reset_ops syscon_reset_ops = {
	.name = "syscon",
	.rating = 50,
	.supported = syscon_reset_supported,
	.reset = syscon_reset_write,
};

static int syscon_reset_probe(const void *fdt, int node)
{
	struct syscon_action *a = NULL;
	uint64_t base = 0, size = 0;
	int regmap = 0;

	if (node < 0)
		return 0;
	a = &actions[fdt_node_check_compatible(fdt, node, "syscon-poweroff") ?
			     1 :
			     0];
	regmap = fdt_node_offset_by_phandle(fdt, fdt_prop_u32(fdt, node,
							      "regmap", 0));
	if (a->reg || regmap < 0 || fdt_reg(fdt, regmap, 0, &base, &size))
		return a->reg ? 0 : -1;

	a->reg = (vaddr_t)base + fdt_prop_u32(fdt, node, "offset", 0);
	a->value = fdt_prop_u32(fdt, node, "value", 0);
	a->mask = fdt_prop_u32(fdt, node, "mask", ~U(0));
	/* Operating systems drive the same controller. */
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_SHARED_RW);

	if (!actions[0].reg != !actions[1].reg) /* the first of the two */
		reset_register(&syscon_reset_ops);
	return 0;
}

static const char *const syscon_reset_compatible[] = {
	"syscon-poweroff",
	"syscon-reboot",
	NULL,
};

DRIVER_DEFINE(syscon_reset) = {
	.name = "syscon-reset",
	.compatible = syscon_reset_compatible,
	.probe = syscon_reset_probe,
};
