// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SiFive GPIO ("sifive,gpio0"): outputs only, which is all <gpio.h> asks for.
 */

#include <driver.h>
#include <fdt_util.h>
#include <gpio.h>
#include <io.h>
#include <libfdt.h>
#include <memregion.h>
#include <types_ext.h>
#include <util.h>

#define REG_OUTPUT_EN 0x08
#define REG_OUTPUT_VAL 0x0c

#define MAX_CHIPS 2

static struct gpio_chip chips[MAX_CHIPS];
static unsigned int nr_chips;

static void sifive_gpio_output(struct gpio_chip *chip, unsigned int line,
			       bool high)
{
	vaddr_t regs = (vaddr_t)chip->priv;

	if (high)
		io_setbits32(regs + REG_OUTPUT_VAL, (uint32_t)BIT(line));
	else
		io_clrbits32(regs + REG_OUTPUT_VAL, (uint32_t)BIT(line));
	io_setbits32(regs + REG_OUTPUT_EN, (uint32_t)BIT(line));
}

static int sifive_gpio_probe(const void *fdt, int node)
{
	struct gpio_chip *chip = &chips[nr_chips];
	uint64_t base = 0, size = 0;

	if (node < 0)
		return 0;
	if (nr_chips == MAX_CHIPS || fdt_reg(fdt, node, 0, &base, &size))
		return -1;
	chip->phandle = fdt_get_phandle(fdt, node);
	chip->nr_lines = fdt_prop_u32(fdt, node, "ngpios", 16);
	chip->output = sifive_gpio_output;
	chip->priv = (void *)(uintptr_t)base;
	gpio_chip_register(chip);
	nr_chips++;
	/* The next stage drives the other lines. */
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_SHARED_RW);
	return 0;
}

static const char *const sifive_gpio_compatible[] = { "sifive,gpio0", NULL };

DRIVER_DEFINE(sifive_gpio) = {
	.name = "sifive-gpio",
	.compatible = sifive_gpio_compatible,
	.stage = DRIVER_STAGE_EARLY,
	.probe = sifive_gpio_probe,
};
