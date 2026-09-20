// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* SiFive UART ("sifive,uart0"), polled. */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>
#include <util.h>

#define REG_TXFIFO 0
#define REG_RXFIFO 4
#define REG_TXCTRL 8
#define REG_RXCTRL 12
#define REG_IE 16
#define REG_DIV 24

#define TXFIFO_FULL U(0x80000000)
#define RXFIFO_EMPTY U(0x80000000)
#define TXCTRL_TXEN 0x1
#define RXCTRL_RXEN 0x1

static vaddr_t uart;

static void sifive_uart_putc(char c)
{
	while (io_read32(uart + REG_TXFIFO) & TXFIFO_FULL)
		;
	io_write32(uart + REG_TXFIFO, (uint8_t)c);
}

static int sifive_uart_getc(void)
{
	uint32_t val = io_read32(uart + REG_RXFIFO);

	return val & RXFIFO_EMPTY ? -1 : (int)(val & 0xff);
}

static const struct console_ops sifive_uart_console = {
	.name = "sifive-uart",
	.putc = sifive_uart_putc,
	.getc = sifive_uart_getc,
};

static int sifive_uart_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	/*
	 * The smallest divisor that does not overshoot: baud = clock / (div +
	 * 1).
	 */
	if (p->clock && p->baud)
		io_write32(uart + REG_DIV,
			   (p->clock + p->baud - 1) / p->baud - 1);
	io_write32(uart + REG_IE, 0);
	io_write32(uart + REG_TXCTRL, TXCTRL_TXEN);
	io_write32(uart + REG_RXCTRL, RXCTRL_RXEN);
	console_register(&sifive_uart_console);
	return 0;
}

static const char *const sifive_uart_compatible[] = {
	"sifive,uart0",
	"sifive,fu540-c000-uart",
	"sifive,fu740-c000-uart",
	NULL,
};

SERIAL_DEFINE(sifive_uart) = {
	.name = "sifive-uart",
	.compatible = sifive_uart_compatible,
	.init = sifive_uart_init,
};
