// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <ipi.h>
#include <reset.h>

static const struct reset_ops *reset;

void reset_register(const struct reset_ops *ops)
{
	reset = ops;
}

const char *reset_name(void)
{
	return reset ? reset->name : "none";
}

bool reset_supported(enum reset_type type)
{
	return reset && reset->supported(type);
}

void __noreturn system_reset(enum reset_type type)
{
	unsigned long self = this_hartid();

	for (unsigned long h = 0; h < CONFIG_PLATFORM_HART_COUNT; h++)
		if (h != self && hart_valid(h))
			ipi_send(h, IPI_EVENT_HALT);

	if (reset_supported(type))
		reset->reset(type);
	hart_halt();
}
