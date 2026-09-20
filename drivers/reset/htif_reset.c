// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Leaving a simulation through HTIF (<htif.h>): a shutdown, and nothing else.
 */

#include <driver.h>
#include <fdt_util.h>
#include <htif.h>
#include <reset.h>

static bool htif_reset_supported(enum reset_type type)
{
	return type == RESET_SHUTDOWN;
}

static void htif_reset(enum reset_type type)
{
	htif_exit(0);
}

static const struct reset_ops htif_reset_ops = {
	.name = "htif",
	.rating = 100,
	.supported = htif_reset_supported,
	.reset = htif_reset,
};

static int htif_reset_probe(const void *fdt, int node)
{
	uint64_t base = 0;

	if (node < 0)
		return 0;
	/*
	 * The words are where the console's are; a "reg" says where, if there
	 * is one.
	 */
	if (!fdt_reg(fdt, node, 0, &base, NULL))
		htif_setup((uintptr_t)base + 8, (uintptr_t)base);
	reset_register(&htif_reset_ops);
	return 0;
}

static const char *const htif_reset_compatible[] = { "ucb,htif0", NULL };

DRIVER_DEFINE(htif_reset_drv) = {
	.name = "htif-reset",
	.compatible = htif_reset_compatible,
	.probe = htif_reset_probe,
};
