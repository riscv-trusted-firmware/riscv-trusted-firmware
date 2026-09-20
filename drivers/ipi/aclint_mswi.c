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

#include <arch/csr.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <ipi.h>
#include <memregion.h>
#include <stdint.h>
#include <types_ext.h>

static vaddr_t msip_base;
/* Which MSIP is a hart's, -1: none. */
static int msip_index[CONFIG_PLATFORM_HART_COUNT];

static vaddr_t msip(unsigned long hartid)
{
	if (hartid >= CONFIG_PLATFORM_HART_COUNT || msip_index[hartid] < 0)
		return 0;
	return msip_base + 4 * (vaddr_t)msip_index[hartid];
}

static void aclint_mswi_send(unsigned long hartid)
{
	vaddr_t reg = msip(hartid);

	if (reg)
		io_write32(reg, 1);
}

static void aclint_mswi_clear(unsigned long hartid)
{
	vaddr_t reg = msip(hartid);

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
		base = CONFIG_IPI_ACLINT_MSWI_ADDR;
		for (unsigned int h = 0; h < CONFIG_PLATFORM_HART_COUNT; h++)
			msip_index[h] =
				(int)h - CONFIG_IPI_ACLINT_MSWI_FIRST_HART;
	} else if (fdt_reg(fdt, node, 0, &base, NULL) ||
		   !fdt_hart_indices(fdt, node, IRQ_M_SOFT, msip_index,
				     CONFIG_PLATFORM_HART_COUNT)) {
		return -1;
	}

	msip_base = (uintptr_t)base;
	ipi_register(&aclint_mswi_ops);
	memregion_add(msip_base, CONFIG_IPI_ACLINT_MSWI_SIZE,
		      MEMREGION_MMODE_RW);
	return 0;
}

static const char *const aclint_mswi_compatible[] = {
	"riscv,aclint-mswi",
	"riscv,clint0",
	"sifive,clint0",
	NULL,
};

DRIVER_DEFINE(aclint_mswi) = {
	.name = "aclint-mswi",
	.compatible = aclint_mswi_compatible,
	.probe_without_node = true,
	.probe = aclint_mswi_probe,
};
