// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * 8250/16550 UART, polled. Register geometry comes from Kconfig; the
 * device-tree path will fill the same struct from the 'compatible' match.
 */

#include <console.h>
#include <drivers/serial/uart8250.h>
#include <io.h>
#include <memregion.h>
#include <stdint.h>
#include <types_ext.h>
#include <util.h>

#define UART_RBR 0 /* receive buffer (read) */
#define UART_THR 0 /* transmit holding (write) */
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_DLL 0 /* divisor latch, LCR[7]=1 */
#define UART_DLM 1

#define UART_LCR_DLAB 0x80
#define UART_LCR_8N1 0x03
#define UART_LSR_DR 0x01
#define UART_LSR_THRE 0x20

struct uart8250 {
	uintptr_t base;
	unsigned int reg_shift;
	unsigned int reg_width;
};

static struct uart8250 uart = {
	.base = CONFIG_SERIAL_UART8250_BASE,
	.reg_shift = CONFIG_SERIAL_UART8250_REG_SHIFT,
	.reg_width = CONFIG_SERIAL_UART8250_REG_WIDTH,
};

static uint32_t reg_read(const struct uart8250 *u, unsigned int reg)
{
	vaddr_t addr = u->base + ((vaddr_t)reg << u->reg_shift);

	if (u->reg_width == 4)
		return io_read32(addr);
	return io_read8(addr);
}

static void reg_write(const struct uart8250 *u, unsigned int reg, uint32_t val)
{
	vaddr_t addr = u->base + ((vaddr_t)reg << u->reg_shift);

	if (u->reg_width == 4)
		io_write32(addr, val);
	else
		io_write8(addr, (uint8_t)val);
}

static void uart8250_putc(char c)
{
	while (!(reg_read(&uart, UART_LSR) & UART_LSR_THRE))
		;
	reg_write(&uart, UART_THR, (uint8_t)c);
}

static int uart8250_getc(void)
{
	if (!(reg_read(&uart, UART_LSR) & UART_LSR_DR))
		return -1;
	return (int)reg_read(&uart, UART_RBR);
}

static const struct console_ops uart8250_console = {
	.name = "uart8250",
	.putc = uart8250_putc,
	.getc = uart8250_getc,
};

void uart8250_console_init(void)
{
	uint32_t divisor = CONFIG_SERIAL_UART8250_CLOCK /
			   (16 * CONFIG_SERIAL_UART8250_BAUD);

	reg_write(&uart, UART_IER, 0);
	reg_write(&uart, UART_LCR, UART_LCR_DLAB);
	reg_write(&uart, UART_DLL, divisor & 0xff);
	reg_write(&uart, UART_DLM, (divisor >> 8) & 0xff);
	reg_write(&uart, UART_LCR, UART_LCR_8N1);
	reg_write(&uart, UART_FCR, 0x07); /* enable + reset FIFOs */
	reg_write(&uart, UART_MCR, 0x03); /* DTR | RTS */

	console_register(&uart8250_console);
	/* The next stage usually drives the same UART. */
	memregion_add(uart.base, SHIFT_UL(0x100, uart.reg_shift),
		      MEMREGION_SHARED_RW);
}
