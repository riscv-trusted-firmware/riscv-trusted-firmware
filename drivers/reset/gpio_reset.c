// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * "gpio-poweroff" and "gpio-restart": a line that, driven active, takes the
 * board down or resets it. The restart line is pulsed the way the binding
 * says: active for "active-delay" ms, inactive for "inactive-delay", active
 * again (100 ms each by default).
 */

#include <driver.h>
#include <fdt_util.h>
#include <gpio.h>
#include <reset.h>
#include <timer.h>
#include <util.h>

static struct gpio_reset {
	struct gpio_line line;
	bool present;
	uint32_t active_ms, inactive_ms;
} poweroff, restart;

static bool gpio_reset_supported(enum reset_type type)
{
	return type == RESET_SHUTDOWN ? poweroff.present : restart.present;
}

static void gpio_reset(enum reset_type type)
{
	const struct gpio_reset *r = type == RESET_SHUTDOWN ? &poweroff :
							      &restart;

	gpio_line_output(&r->line, true);
	if (type == RESET_SHUTDOWN)
		return;
	timer_udelay(ULL(1000) * r->active_ms);
	gpio_line_output(&r->line, false);
	timer_udelay(ULL(1000) * r->inactive_ms);
	gpio_line_output(&r->line, true);
}

static const struct reset_ops gpio_reset_ops = {
	.name = "gpio",
	.rating = 100,
	.supported = gpio_reset_supported,
	.reset = gpio_reset,
};

static int gpio_reset_probe(const void *fdt, int node)
{
	struct gpio_reset *r = NULL;

	if (node < 0)
		return 0;
	r = fdt_node_check_compatible(fdt, node, "gpio-poweroff") ? &restart :
								    &poweroff;
	if (r->present)
		return 0;
	if (gpio_line_from_fdt(fdt, node, "gpios", 0, &r->line))
		return -1;
	r->active_ms = fdt_prop_u32(fdt, node, "active-delay", 100);
	r->inactive_ms = fdt_prop_u32(fdt, node, "inactive-delay", 100);
	r->present = true;
	if (!(r == &poweroff ? restart.present : poweroff.present))
		reset_register(&gpio_reset_ops);
	return 0;
}

static const char *const gpio_reset_compatible[] = { "gpio-poweroff",
						     "gpio-restart", NULL };

DRIVER_DEFINE(gpio_reset_drv) = {
	.name = "gpio-reset",
	.compatible = gpio_reset_compatible,
	.stage = DRIVER_STAGE_LATE,
	.probe = gpio_reset_probe,
};
