// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V IMSIC ("riscv,imsics"), machine-level interrupt files. The
 * monitor does not take MSIs: each hart turns interrupt delivery from its
 * M-level file off, through the Smaia indirect CSRs. The S-level files are
 * S-mode's business.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <irqchip.h>
#include <log.h>
#include <memregion.h>

#include "irqchip_internal.h"

#define CSR_MISELECT 0x350
#define CSR_MIREG 0x351
#define IMSIC_EIDELIVERY 0x70
#define IMSIC_EITHRESHOLD 0x72

static void imsic_hart_init(void)
{
	unsigned long val = 0;

	/* No Smaia on this hart, no interrupt file either. */
	if (!csr_probe(CSR_MISELECT, &val))
		return;
	csr_write(CSR_MISELECT, IMSIC_EIDELIVERY);
	csr_write(CSR_MIREG, 0);
	/* Were it ever turned on: a threshold of 1 lets nothing through. */
	csr_write(CSR_MISELECT, IMSIC_EITHRESHOLD);
	csr_write(CSR_MIREG, 1);
}

static int imsic_probe(const void *fdt)
{
	uint64_t base = 0, size = 0;
	int node = -1;

	while (fdt) {
		node = fdt_node_offset_by_compatible(fdt, node, "riscv,imsics");
		if (node < 0)
			break;
		if (!fdt_node_enabled(fdt, node) ||
		    !irqchip_is_mlevel(fdt, node))
			continue;
		for (int i = 0; !fdt_reg(fdt, node, i, &base, &size); i++)
			memregion_add((unsigned long)base, (unsigned long)size,
				      MEMREGION_MMODE_RW);
		irqchip_set_hart_init(imsic_hart_init);
		pr_info("imsic: machine-level interrupt files left off\n");
		break;
	}
	return 0;
}

DRIVER_DEFINE(imsic) = {
	.name = "imsic",
	.probe = imsic_probe,
};
