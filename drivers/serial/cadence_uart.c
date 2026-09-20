// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* Cadence UART ("cdns,uart-r1p12"), polled. */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>
#include <util.h>

#define REG_CTRL 0x00
#define REG_MODE 0x04
#define REG_IDR 0x0c
#define REG_BRGR 0x18
#define REG_CSR 0x2c
#define REG_FIFO 0x30
#define REG_BDIVR 0x34

#define CTRL_RXRES 0x01
#define CTRL_TXRES 0x02
#define CTRL_RXEN 0x04
#define CTRL_TXEN 0x10
#define MODE_PAR_NONE 0x20 /* 8 bits, 1 stop bit, the clock undivided */
#define CSR_REMPTY 0x02
#define CSR_TFUL 0x10

static vaddr_t uart;

static void cadence_uart_putc(char c)
{
	while (io_read32(uart + REG_CSR) & CSR_TFUL)
		;
	io_write32(uart + REG_FIFO, (uint8_t)c);
}

static int cadence_uart_getc(void)
{
	return io_read32(uart + REG_CSR) & CSR_REMPTY ?
		       -1 :
		       (int)(io_read32(uart + REG_FIFO) & 0xff);
}

static const struct console_ops cadence_uart_console = {
	.name = "cadence-uart",
	.putc = cadence_uart_putc,
	.getc = cadence_uart_getc,
};

static int cadence_uart_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	io_write32(uart + REG_IDR, ~U(0));
	io_write32(uart + REG_MODE, MODE_PAR_NONE);
	/*
	 * baud = clock / (BRGR * (BDIVR + 1)): BDIVR at its reset 15, BRGR to
	 * match.
	 */
	if (p->clock && p->baud) {
		uint32_t brgr = p->clock / (16 * p->baud);

		io_write32(uart + REG_BDIVR, 15);
		io_write32(uart + REG_BRGR, brgr ? brgr : 1);
	}
	io_write32(uart + REG_CTRL, CTRL_RXRES | CTRL_TXRES);
	io_write32(uart + REG_CTRL, CTRL_RXEN | CTRL_TXEN);
	console_register(&cadence_uart_console);
	return 0;
}

static const char *const cadence_uart_compatible[] = {
	"cdns,uart-r1p8",
	"cdns,uart-r1p12",
	"starfive,jh8100-uart",
	NULL,
};

SERIAL_DEFINE(cadence_uart) = {
	.name = "cadence-uart",
	.compatible = cadence_uart_compatible,
	.init = cadence_uart_init,
};
