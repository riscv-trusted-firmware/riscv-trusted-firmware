// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* GPIO chips and lines, see <gpio.h>. */

#include <gpio.h>
#include <libfdt.h>

#define GPIO_ACTIVE_LOW 0x1

static struct gpio_chip *chips;

void gpio_chip_register(struct gpio_chip *chip)
{
	chip->next = chips;
	chips = chip;
}

int gpio_line_from_fdt(const void *fdt, int node, const char *prop,
		       unsigned int index, struct gpio_line *line)
{
	int len = 0;
	const fdt32_t *cells = fdt_getprop(fdt, node, prop, &len);

	/*
	 * <&controller line flags>: two cells after the phandle, as nearly
	 * everywhere.
	 */
	if (!cells || (int)(12 * (index + 1)) > len)
		return -1;
	cells += 3 * index;
	for (struct gpio_chip *chip = chips; chip; chip = chip->next) {
		if (chip->phandle != fdt32_to_cpu(cells[0]))
			continue;
		if (fdt32_to_cpu(cells[1]) >= chip->nr_lines)
			return -1;
		line->chip = chip;
		line->line = fdt32_to_cpu(cells[1]);
		line->active_low = fdt32_to_cpu(cells[2]) & GPIO_ACTIVE_LOW;
		return 0;
	}
	return -1;
}

void gpio_line_output(const struct gpio_line *line, bool active)
{
	line->chip->output(line->chip, line->line, active != line->active_low);
}
