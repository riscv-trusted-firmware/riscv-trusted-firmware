/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef DRIVERS_SERIAL_UART8250_H
#define DRIVERS_SERIAL_UART8250_H

/*
 * Initialise the UART described by CONFIG_SERIAL_UART8250_* and make it the
 * console.
 */
void uart8250_console_init(void);

#endif
