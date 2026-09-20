// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V IMSIC ("riscv,imsics"), machine-level interrupt files.
 *
 * The monitor takes no device MSIs, but the M-level files make a good IPI:
 * a hart enables its file for one interrupt identity, and a write of that
 * identity to the file raises the target's machine external interrupt,
 * which is claimed through mtopei. Where an M-level IMSIC exists this is
 * preferred over the ACLINT MSWI. The S-level files are S-mode's business.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <ipi.h>
#include <log.h>
#include <memregion.h>
#include <util.h>

#include "irqchip_internal.h"

#define CSR_MISELECT 0x350
#define CSR_MIREG 0x351
#define CSR_MTOPEI 0x35c
#define IMSIC_EIDELIVERY 0x70
#define IMSIC_EITHRESHOLD 0x72
#define IMSIC_EIE0 0xc0

#define IMSIC_FILE_SIZE UL(0x1000)
#define IMSIC_SETEIPNUM_LE 0x00
#define IMSIC_IPI_ID 1

/* Address of each hart's M-level interrupt file, 0: none. */
static uintptr_t files[CONFIG_PLATFORM_HART_COUNT];

static void imsic_hart_init(void)
{
	/* Deliver, with no threshold, the one identity that is enabled. */
	csr_write(CSR_MISELECT, IMSIC_EIE0);
	csr_write(CSR_MIREG, BIT(IMSIC_IPI_ID));
	csr_write(CSR_MISELECT, IMSIC_EITHRESHOLD);
	csr_write(CSR_MIREG, 0);
	csr_write(CSR_MISELECT, IMSIC_EIDELIVERY);
	csr_write(CSR_MIREG, 1);
}

static void imsic_ipi_send(unsigned long hartid)
{
	if (hartid < CONFIG_PLATFORM_HART_COUNT && files[hartid])
		io_write32(files[hartid] + IMSIC_SETEIPNUM_LE, IMSIC_IPI_ID);
}

/* Claiming the top interrupt clears its pending bit; until none is left. */
static void imsic_ipi_clear(unsigned long hartid)
{
	while (csr_swap(CSR_MTOPEI, 0))
		;
}

static const struct ipi_ops imsic_ipi_ops = {
	.name = "imsic",
	.rating = 200,
	.irq = MIP_MEIP,
	.hart_init = imsic_hart_init,
	.send = imsic_ipi_send,
	.clear = imsic_ipi_clear,
};

/*
 * The files of a "reg" region belong to consecutive entries of
 * "interrupts-extended", one page each (no guest files at machine level).
 */
static unsigned int imsic_map_files(const void *fdt, int node)
{
	unsigned int mapped = 0;
	uint64_t base = 0, size = 0;
	int hart_idx = 0;

	for (int i = 0; !fdt_reg(fdt, node, i, &base, &size); i++) {
		memregion_add((unsigned long)base, (unsigned long)size,
			      MEMREGION_MMODE_RW);
		for (uint64_t off = 0; off < size;
		     off += IMSIC_FILE_SIZE, hart_idx++) {
			unsigned long hartid = 0;
			uint32_t irq = 0;

			if (fdt_hart_irq(fdt, node, hart_idx, &hartid, &irq))
				break;
			if (hartid < CONFIG_PLATFORM_HART_COUNT) {
				files[hartid] = (uintptr_t)(base + off);
				mapped++;
			}
		}
	}
	return mapped;
}

static int imsic_probe(const void *fdt)
{
	unsigned long val = 0;
	int node = -1;

	/* No Smaia on the boot hart, no interrupt files to talk to. */
	if (!fdt || !csr_probe(CSR_MISELECT, &val))
		return 0;

	while ((node = fdt_node_offset_by_compatible(fdt, node,
						     "riscv,imsics")) >= 0) {
		if (!fdt_node_enabled(fdt, node) ||
		    !irqchip_is_mlevel(fdt, node))
			continue;
		if (imsic_map_files(fdt, node)) {
			ipi_register(&imsic_ipi_ops);
			pr_dbg("imsic: machine-level files carry the IPIs\n");
		}
		break;
	}
	return 0;
}

DRIVER_DEFINE(imsic) = {
	.name = "imsic",
	.probe = imsic_probe,
};
