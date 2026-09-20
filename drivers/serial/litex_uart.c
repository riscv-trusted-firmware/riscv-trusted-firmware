// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* LiteX UART ("litex,liteuart"), polled. The speed is fixed in the gateware. */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>

#define REG_RXTX 0
#define REG_TXFULL 4
#define REG_RXEMPTY 8
#define REG_EV_PENDING 16
#define REG_EV_ENABLE 20

#define EV_RX 0x2

static vaddr_t uart;

static void litex_uart_putc(char c)
{
	while (io_read32(uart + REG_TXFULL))
		;
	io_write32(uart + REG_RXTX, (uint8_t)c);
}

static int litex_uart_getc(void)
{
	int c = 0;

	if (io_read32(uart + REG_RXEMPTY))
		return -1;
	c = (int)(io_read32(uart + REG_RXTX) & 0xff);
	/* Acknowledging the event is what moves the receive FIFO on. */
	io_write32(uart + REG_EV_PENDING, EV_RX);
	return c;
}

static const struct console_ops litex_uart_console = {
	.name = "litex-uart",
	.putc = litex_uart_putc,
	.getc = litex_uart_getc,
};

static int litex_uart_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	io_write32(uart + REG_EV_ENABLE, 0);
	console_register(&litex_uart_console);
	return 0;
}

static const char *const litex_uart_compatible[] = { "litex,liteuart", NULL };

SERIAL_DEFINE(litex_uart) = {
	.name = "litex-uart",
	.compatible = litex_uart_compatible,
	.init = litex_uart_init,
};
