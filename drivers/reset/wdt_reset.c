// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Resetting through a watchdog, where a SoC has nothing more direct: the
 * Allwinner D1's ("allwinner,sun20i-d1-wdt-reset") has a software reset
 * bit behind a key, the Andes ATCWDT200 ("andestech,atcwdt200") is armed
 * with its shortest time-out and left to expire. Reboots only.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <memregion.h>
#include <reset.h>
#include <stdint.h>
#include <types_ext.h>
#include <util.h>

#define SUNXI_SOFT_RST 0x08
#define SUNXI_KEY U(0x16aa0000)
#define SUNXI_SOFT_RST_EN 0x1

#define ATCWDT_CTRL 0x10
#define ATCWDT_WREN 0x18
#define ATCWDT_WP_NUM 0x5aa5
/* Enabled, reset enabled, APB clock, the shortest interrupt and reset times. */
#define ATCWDT_CTRL_RESET_NOW (0x1 | 0x2 | 0x8)

static vaddr_t wdt;
static bool is_sunxi;

static bool wdt_reset_supported(enum reset_type type)
{
	return type != RESET_SHUTDOWN;
}

static void wdt_reset(enum reset_type type)
{
	if (is_sunxi) {
		io_write32(wdt + SUNXI_SOFT_RST, SUNXI_KEY | SUNXI_SOFT_RST_EN);
		return;
	}
	/* Every write to the control register takes the write enable first. */
	io_write32(wdt + ATCWDT_WREN, ATCWDT_WP_NUM);
	io_write32(wdt + ATCWDT_CTRL, ATCWDT_CTRL_RESET_NOW);
}

static const struct reset_ops wdt_reset_ops = {
	.name = "watchdog",
	.rating = 50,
	.supported = wdt_reset_supported,
	.reset = wdt_reset,
};

static int wdt_reset_probe(const void *fdt, int node)
{
	uint64_t base = 0, size = 0;

	if (node < 0 || wdt)
		return 0;
	if (fdt_reg(fdt, node, 0, &base, &size))
		return -1;
	wdt = (vaddr_t)base;
	is_sunxi = !fdt_node_check_compatible(fdt, node,
					      "allwinner,sun20i-d1-wdt-reset");
	reset_register(&wdt_reset_ops);
	/* A watchdog is the operating system's to pet as well. */
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_SHARED_RW);
	return 0;
}

static const char *const wdt_reset_compatible[] = {
	"allwinner,sun20i-d1-wdt-reset",
	"andestech,atcwdt200",
	NULL,
};

DRIVER_DEFINE(wdt_reset_drv) = {
	.name = "wdt-reset",
	.compatible = wdt_reset_compatible,
	.probe = wdt_reset_probe,
};
