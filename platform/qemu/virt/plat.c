// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * QEMU virt machine.
 */

#include <platform.h>
#include <drivers/serial/uart8250.h>

void plat_early_init(void)
{
	uart8250_console_init();
}

void plat_init(void)
{
}
