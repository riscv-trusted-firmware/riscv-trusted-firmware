// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * 8250/16550 UART, polled. Without a device tree node to go by, the Kconfig
 * geometry applies, if there is one.
 */

#include <console.h>
#include <io.h>
#include <memregion.h>
#include <serial.h>
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
	uint32_t clock;
	uint32_t baud;
};

static struct uart8250 uart;

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

static int uart8250_init(const struct serial_params *p)
{
	uint32_t divisor = 0;

	if (p) {
		uart = (struct uart8250){
			.base = p->base,
			.reg_shift = p->reg_shift,
			.reg_width = p->reg_width ? p->reg_width : 1,
			.clock = p->clock,
			.baud = p->baud ? p->baud : CONFIG_SERIAL_UART8250_BAUD,
		};
	} else {
		uart = (struct uart8250){
			.base = CONFIG_SERIAL_UART8250_BASE,
			.reg_shift = CONFIG_SERIAL_UART8250_REG_SHIFT,
			.reg_width = CONFIG_SERIAL_UART8250_REG_WIDTH,
			.clock = CONFIG_SERIAL_UART8250_CLOCK,
			.baud = CONFIG_SERIAL_UART8250_BAUD,
		};
	}
	if (!uart.base)
		return -1;
	/*
	 * A node's registers are the serial core's to declare; these are ours.
	 */
	if (!p)
		memregion_add(uart.base, SHIFT_UL(0x100, uart.reg_shift),
			      MEMREGION_SHARED_RW);

	reg_write(&uart, UART_IER, 0);
	/* No clock to divide: whoever ran before has set the speed. */
	divisor = uart.clock / (16 * uart.baud);
	if (divisor) {
		reg_write(&uart, UART_LCR, UART_LCR_DLAB);
		reg_write(&uart, UART_DLL, divisor & 0xff);
		reg_write(&uart, UART_DLM, (divisor >> 8) & 0xff);
	}
	reg_write(&uart, UART_LCR, UART_LCR_8N1);
	reg_write(&uart, UART_FCR, 0x07); /* enable + reset FIFOs */
	reg_write(&uart, UART_MCR, 0x03); /* DTR | RTS */

	console_register(&uart8250_console);
	return 0;
}

static const char *const uart8250_compatible[] = {
	"ns16550a", "ns16550", "snps,dw-apb-uart", "intel,xscale-uart", NULL,
};

SERIAL_DEFINE(uart8250_serial) = {
	.name = "uart8250",
	.compatible = uart8250_compatible,
	.init = uart8250_init,
};
