/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef GPIO_H
#define GPIO_H

/*
 * GPIO lines, as far as the monitor needs them: driving an output that
 * resets or powers off the board. A controller driver registers a chip
 * (early stage); a user finds its line through a "gpios"-style property,
 * <&controller line flags>, and drives it by its logical value, the
 * active-low flag taken care of.
 */

#include <stdbool.h>
#include <stdint.h>

struct gpio_chip {
	uint32_t phandle; /* of the controller's node */
	unsigned int nr_lines;
	/* Make 'line' an output that drives 'high'. */
	void (*output)(struct gpio_chip *chip, unsigned int line, bool high);
	void *priv;
	struct gpio_chip *next;
};

struct gpio_line {
	struct gpio_chip *chip;
	unsigned int line;
	bool active_low;
};

void gpio_chip_register(struct gpio_chip *chip);
/* Entry 'index' of property 'prop' of 'node'; -1 when there is no such line. */
int gpio_line_from_fdt(const void *fdt, int node, const char *prop,
		       unsigned int index, struct gpio_line *line);
void gpio_line_output(const struct gpio_line *line, bool active);

#endif
