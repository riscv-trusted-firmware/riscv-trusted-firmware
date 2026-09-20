// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* Gaisler APBUART ("gaisler,apbuart"), polled. */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>

#define REG_DATA 0
#define REG_STATUS 4
#define REG_CTRL 8
#define REG_SCALER 12

#define STATUS_DATAREADY 0x001
#define STATUS_FIFOFULL 0x200
#define CTRL_RE 0x001
#define CTRL_TE 0x002

static vaddr_t uart;

static void gaisler_uart_putc(char c)
{
	while (io_read32(uart + REG_STATUS) & STATUS_FIFOFULL)
		;
	io_write32(uart + REG_DATA, (uint8_t)c);
}

static int gaisler_uart_getc(void)
{
	return io_read32(uart + REG_STATUS) & STATUS_DATAREADY ?
		       (int)(io_read32(uart + REG_DATA) & 0xff) :
		       -1;
}

static const struct console_ops gaisler_uart_console = {
	.name = "gaisler-uart",
	.putc = gaisler_uart_putc,
	.getc = gaisler_uart_getc,
};

static int gaisler_uart_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	/* The scaler counts down at the clock rate, eight ticks to a bit. */
	if (p->clock && p->baud)
		io_write32(uart + REG_SCALER, p->clock / (8 * p->baud) - 1);
	io_write32(uart + REG_CTRL, CTRL_RE | CTRL_TE);
	console_register(&gaisler_uart_console);
	return 0;
}

static const char *const gaisler_uart_compatible[] = { "gaisler,apbuart",
						       NULL };

SERIAL_DEFINE(gaisler_uart) = {
	.name = "gaisler-uart",
	.compatible = gaisler_uart_compatible,
	.init = gaisler_uart_init,
};
