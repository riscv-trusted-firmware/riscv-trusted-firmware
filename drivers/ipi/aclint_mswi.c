// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V ACLINT MSWI ("riscv,aclint-mswi") and the software interrupt half
 * of the SiFive CLINT ("riscv,clint0", "sifive,clint0"): one MSIP register
 * per hart, numbered by the hart's position among the machine software
 * interrupts the node lists. Without a node the Kconfig geometry applies.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <ipi.h>
#include <memregion.h>
#include <stdint.h>
#include <types_ext.h>

static vaddr_t msip_base;
/* Which MSIP belongs to a hart (by hart index), -1: none. */
static int msip_of[CONFIG_PLATFORM_HART_COUNT];

/* 0: the hart has none. */
static vaddr_t msip(unsigned int index)
{
	if (index >= CONFIG_PLATFORM_HART_COUNT || msip_of[index] < 0)
		return 0;
	return msip_base + 4 * (vaddr_t)msip_of[index];
}

static void aclint_mswi_send(unsigned int index)
{
	vaddr_t reg = msip(index);

	if (reg)
		io_write32(reg, 1);
}

static void aclint_mswi_clear(unsigned int index)
{
	vaddr_t reg = msip(index);

	if (reg)
		io_write32(reg, 0);
}

static const struct ipi_ops aclint_mswi_ops = {
	.name = "aclint-mswi",
	.rating = 100,
	.irq = MIP_MSIP,
	.send = aclint_mswi_send,
	.clear = aclint_mswi_clear,
};

static int aclint_mswi_probe(const void *fdt, int node)
{
	uint64_t base = 0;

	if (msip_base)
		return 0;

	if (node < 0) {
		if (!CONFIG_IPI_ACLINT_MSWI_ADDR)
			return 0;
		base = CONFIG_IPI_ACLINT_MSWI_ADDR;
		/* Registers in hart id order, from the first hart's on. */
		for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
			msip_of[i] = -1;
			if (hart_by_index(i))
				msip_of[i] = (int)hart_id_of(i) -
					     CONFIG_IPI_ACLINT_MSWI_FIRST_HART;
		}
	} else if (fdt_reg(fdt, node, 0, &base, NULL) ||
		   !fdt_hart_positions(fdt, node, IRQ_M_SOFT, hart_index,
				       msip_of, CONFIG_PLATFORM_HART_COUNT)) {
		return -1;
	}

	msip_base = (uintptr_t)base;
	ipi_register(&aclint_mswi_ops);
	memregion_add(msip_base, CONFIG_IPI_ACLINT_MSWI_SIZE,
		      MEMREGION_MMODE_RW);
	return 0;
}

static const char *const aclint_mswi_compatible[] = {
	"riscv,aclint-mswi",	  "riscv,clint0", "sifive,clint0",
	"thead,c900-aclint-mswi", NULL,
};

DRIVER_DEFINE(aclint_mswi) = {
	.name = "aclint-mswi",
	.compatible = aclint_mswi_compatible,
	.probe_without_node = true,
	.probe = aclint_mswi_probe,
};
