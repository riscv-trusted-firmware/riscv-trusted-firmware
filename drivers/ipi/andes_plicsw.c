// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Andes PLICSW ("andestech,plicsw"): a PLIC whose interrupt sources are set
 * pending by software, wired to the harts' machine software interrupts.
 * Source n + 1 is the IPI of the hart in position n, enabled for that
 * hart's context alone; claiming it takes the interrupt back.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <ipi.h>
#include <memregion.h>
#include <types_ext.h>
#include <util.h>

#define PLICSW_PRIORITY 0x4
#define PLICSW_PENDING 0x1000
#define PLICSW_ENABLE 0x2000
#define PLICSW_ENABLE_STRIDE 0x80
#define PLICSW_CONTEXT 0x200000
#define PLICSW_CONTEXT_STRIDE 0x1000
#define PLICSW_CLAIM 0x4

static vaddr_t plicsw;
/* A hart's position among the device's harts (by hart index), -1: none. */
static int position_of[CONFIG_PLATFORM_HART_COUNT];

static vaddr_t reg(vaddr_t off)
{
	return plicsw + off;
}

static void plicsw_send(unsigned int index)
{
	int pos = index < CONFIG_PLATFORM_HART_COUNT ? position_of[index] : -1;

	if (pos >= 0)
		io_write32(reg(PLICSW_PENDING +
			       4 * (((unsigned int)pos + 1) / 32)),
			   (uint32_t)BIT(((unsigned int)pos + 1) % 32));
}

static void plicsw_clear(unsigned int index)
{
	int pos = position_of[index];
	vaddr_t claim = 0;

	if (pos < 0)
		return;
	claim = reg(PLICSW_CONTEXT + PLICSW_CLAIM +
		    PLICSW_CONTEXT_STRIDE * (unsigned int)pos);
	/* Claim, which is what drops the interrupt, and complete. */
	io_write32(claim, io_read32(claim));
}

static const struct ipi_ops plicsw_ops = {
	.name = "andes-plicsw",
	.rating = 150,
	.irq = MIP_MSIP,
	.send = plicsw_send,
	.clear = plicsw_clear,
};

static int plicsw_probe(const void *fdt, int node)
{
	uint64_t base = 0, size = 0;

	if (node < 0 || plicsw)
		return 0;
	if (fdt_reg(fdt, node, 0, &base, &size) ||
	    !fdt_hart_positions(fdt, node, IRQ_M_SOFT, hart_index, position_of,
				CONFIG_PLATFORM_HART_COUNT))
		return -1;
	plicsw = (uintptr_t)base;

	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
		unsigned int id = 0;

		if (position_of[i] < 0)
			continue;
		id = (unsigned int)position_of[i] + 1;
		io_write32(reg(PLICSW_PRIORITY * id), 1);
		io_write32(reg(PLICSW_ENABLE + PLICSW_ENABLE_STRIDE * (id - 1) +
			       4 * (id / 32)),
			   (uint32_t)BIT(id % 32));
	}
	ipi_register(&plicsw_ops);
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_MMODE_RW);
	return 0;
}

static const char *const plicsw_compatible[] = { "andestech,plicsw", NULL };

DRIVER_DEFINE(andes_plicsw) = {
	.name = "andes-plicsw",
	.compatible = plicsw_compatible,
	.probe = plicsw_probe,
};
