/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef DRIVERS_SERIAL_UART8250_H
#define DRIVERS_SERIAL_UART8250_H

/*
 * Make the UART that /chosen/stdout-path of 'fdt' names the console; the one
 * CONFIG_SERIAL_UART8250_* describe when there is no tree (NULL) or no such
 * UART in it.
 */
void uart8250_console_init(const void *fdt);

#endif
