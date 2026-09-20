// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* Shakti UART ("shakti,uart0"), polled. */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>

/*
 * Registers of different widths: the baud divisor is 16 bits, data and status
 * 8.
 */
#define REG_BAUD 0x00
#define REG_TX 0x04
#define REG_RX 0x08
#define REG_STATUS 0x0c

#define STATUS_TX_FULL 0x2
#define STATUS_RX_NOT_EMPTY 0x4

static vaddr_t uart;

static void shakti_uart_putc(char c)
{
	while (io_read8(uart + REG_STATUS) & STATUS_TX_FULL)
		;
	io_write8(uart + REG_TX, (uint8_t)c);
}

static int shakti_uart_getc(void)
{
	if (!(io_read8(uart + REG_STATUS) & STATUS_RX_NOT_EMPTY))
		return -1;
	return io_read8(uart + REG_RX);
}

static const struct console_ops shakti_uart_console = {
	.name = "shakti-uart",
	.putc = shakti_uart_putc,
	.getc = shakti_uart_getc,
};

static int shakti_uart_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	if (p->clock && p->baud)
		io_write16(uart + REG_BAUD,
			   (uint16_t)(p->clock / (16 * p->baud)));
	console_register(&shakti_uart_console);
	return 0;
}

static const char *const shakti_uart_compatible[] = { "shakti,uart0", NULL };

SERIAL_DEFINE(shakti_uart) = {
	.name = "shakti-uart",
	.compatible = shakti_uart_compatible,
	.init = shakti_uart_init,
};
