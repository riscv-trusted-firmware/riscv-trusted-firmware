// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The generic platform: nothing here knows a board. The console is the
 * UART the device tree points at, every driver the build has is tried
 * against the tree, and the next stage comes from the hand-over block or
 * the configuration. A board that needs more than its tree can say gets a
 * platform directory of its own.
 */

#include <platform.h>
#include <serial.h>

void plat_early_init(const void *fdt)
{
	serial_console_init(fdt);
}

int plat_fdt_prepare(void *fdt)
{
	return 0;
}

void plat_init(void)
{
}
