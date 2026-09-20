// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Renesas SCIF ("renesas,scif-r9a07g043", the RZ/Five's), polled. The port
 * is taken as the previous stage left it: clocks, pins and speed are its
 * business, and a console has been there before the monitor on every board
 * that has one of these.
 */

#include <console.h>
#include <io.h>
#include <serial.h>
#include <types_ext.h>

#define REG_SCR 0x04 /* 16 bits */
#define REG_FTDR 0x06 /* 8 bits */
#define REG_FSR 0x08 /* 16 bits */
#define REG_FRDR 0x0a /* 8 bits */

#define SCR_RE 0x10
#define SCR_TE 0x20
#define FSR_DR 0x01
#define FSR_RDF 0x02
#define FSR_TDFE 0x20
#define FSR_TEND 0x40

static vaddr_t scif;

static uint16_t fsr_read(void)
{
	return io_read16(scif + REG_FSR);
}

/* A flag is cleared by writing 0 to it after having read it as 1. */
static void fsr_clear(uint16_t flags)
{
	io_write16(scif + REG_FSR, (uint16_t)(fsr_read() & ~flags));
}

static void scif_putc(char c)
{
	while (!(fsr_read() & FSR_TDFE))
		;
	io_write8(scif + REG_FTDR, (uint8_t)c);
	fsr_clear(FSR_TDFE | FSR_TEND);
}

static int scif_getc(void)
{
	int c = 0;

	if (!(fsr_read() & (FSR_DR | FSR_RDF)))
		return -1;
	c = io_read8(scif + REG_FRDR);
	fsr_clear(FSR_DR | FSR_RDF);
	return c;
}

static const struct console_ops scif_console = {
	.name = "renesas-scif",
	.putc = scif_putc,
	.getc = scif_getc,
};

static int scif_init(const struct serial_params *p)
{
	if (!p || !p->base)
		return -1;
	scif = p->base;
	/* Transmitter and receiver on, their interrupts off. */
	io_write16(scif + REG_SCR, SCR_RE | SCR_TE);
	console_register(&scif_console);
	return 0;
}

static const char *const scif_compatible[] = { "renesas,scif-r9a07g043", NULL };

SERIAL_DEFINE(renesas_scif) = {
	.name = "renesas-scif",
	.compatible = scif_compatible,
	.init = scif_init,
};
