// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Xilinx UART Lite ("xlnx,xps-uartlite-1.00.a"), polled. The speed is fixed in
 * the hardware.
 */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>

#define REG_RX 0
#define REG_TX 4
#define REG_STATUS 8
#define REG_CTRL 12

#define STATUS_RX_VALID 0x01
#define STATUS_TX_FULL 0x08
#define CTRL_RST_FIFOS 0x03

static vaddr_t uart;

static void uartlite_putc(char c)
{
	while (io_read32(uart + REG_STATUS) & STATUS_TX_FULL)
		;
	io_write32(uart + REG_TX, (uint8_t)c);
}

static int uartlite_getc(void)
{
	return io_read32(uart + REG_STATUS) & STATUS_RX_VALID ?
		       (int)(io_read32(uart + REG_RX) & 0xff) :
		       -1;
}

static const struct console_ops uartlite_console = {
	.name = "uartlite",
	.putc = uartlite_putc,
	.getc = uartlite_getc,
};

static int uartlite_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	uart = p->base;
	io_write32(uart + REG_CTRL, CTRL_RST_FIFOS); /* and no interrupt */
	console_register(&uartlite_console);
	return 0;
}

static const char *const uartlite_compatible[] = {
	"xlnx,xps-uartlite-1.00.a",
	"xlnx,opb-uartlite-1.00.b",
	NULL,
};

SERIAL_DEFINE(uartlite) = {
	.name = "uartlite",
	.compatible = uartlite_compatible,
	.init = uartlite_init,
};
