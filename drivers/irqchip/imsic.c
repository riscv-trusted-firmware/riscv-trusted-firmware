// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V IMSIC ("riscv,imsics"), machine-level interrupt files.
 *
 * The M-level files make a good IPI: a hart enables its file for one
 * interrupt identity, and a write of that identity to the file raises the
 * target's machine external interrupt, which is claimed through mtopei.
 * Where an M-level IMSIC exists this is preferred over the ACLINT MSWI. The
 * identities after it are MSIs for the monitor's drivers
 * (irqchip_msi_request(): the RPMI P2A doorbell). The S-level files are
 * S-mode's business.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <ipi.h>
#include <irqchip.h>
#include <log.h>
#include <memregion.h>
#include <spinlock.h>
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
#define IMSIC_MSI_FIRST_ID 2
#define IMSIC_MSIS 4

/* Address of each hart's M-level interrupt file (by hart index), 0: none. */
static uintptr_t files[CONFIG_PLATFORM_HART_COUNT];

/* The MSIs handed out: identity IMSIC_MSI_FIRST_ID + i, enabled on one hart. */
static struct {
	void (*handler)(void *arg);
	void *arg;
} msis[IMSIC_MSIS];
static unsigned int nr_msis;
static unsigned long msi_ids[CONFIG_PLATFORM_HART_COUNT]; /* eie0 bits */
static unsigned long msi_lock = SPINLOCK_UNLOCK;

static void imsic_hart_init(void)
{
	/* Deliver, with no threshold, the identities that are enabled. */
	csr_write(CSR_MISELECT, IMSIC_EIE0);
	csr_write(CSR_MIREG, BIT(IMSIC_IPI_ID) | msi_ids[this_hart_index()]);
	csr_write(CSR_MISELECT, IMSIC_EITHRESHOLD);
	csr_write(CSR_MIREG, 0);
	csr_write(CSR_MISELECT, IMSIC_EIDELIVERY);
	csr_write(CSR_MIREG, 1);
}

static void imsic_ipi_send(unsigned int index)
{
	if (index < CONFIG_PLATFORM_HART_COUNT && files[index])
		io_write32(files[index] + IMSIC_SETEIPNUM_LE, IMSIC_IPI_ID);
}

/*
 * Claiming the top interrupt clears its pending bit; until none is left.
 * What is not the IPI is somebody's MSI.
 */
static void imsic_ipi_clear(unsigned int index)
{
	unsigned long top = 0;

	while ((top = csr_swap(CSR_MTOPEI, 0))) {
		unsigned long msi = (top >> 16) - IMSIC_MSI_FIRST_ID;

		if (msi < IMSIC_MSIS && msis[msi].handler)
			msis[msi].handler(msis[msi].arg);
	}
}

int irqchip_msi_request(void (*handler)(void *arg), void *arg,
			struct irqchip_msi *msi)
{
	unsigned int self = this_hart_index(), id = 0;

	if (!files[self])
		return -1;
	spin_lock(&msi_lock);
	if (nr_msis == IMSIC_MSIS) {
		spin_unlock(&msi_lock);
		return -1;
	}
	id = IMSIC_MSI_FIRST_ID + nr_msis;
	msis[nr_msis].arg = arg;
	msis[nr_msis].handler = handler;
	nr_msis++;
	spin_unlock(&msi_lock);

	/* This hart's from now on, however often it stops and starts. */
	msi_ids[self] |= BIT(id);
	csr_write(CSR_MISELECT, IMSIC_EIE0);
	csr_set(CSR_MIREG, BIT(id));
	msi->addr = files[self] + IMSIC_SETEIPNUM_LE;
	msi->data = id;
	return 0;
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
		/*
		 * The monitor's alone, unless the platform has a device that
		 * is S-mode software (a test model) send the monitor MSIs.
		 */
#ifdef CONFIG_IRQCHIP_IMSIC_PROTECT
		memregion_add((unsigned long)base, (unsigned long)size,
			      MEMREGION_MMODE_RW);
#else
		memregion_add((unsigned long)base, (unsigned long)size,
			      MEMREGION_SHARED_RW);
#endif
		for (uint64_t off = 0; off < size;
		     off += IMSIC_FILE_SIZE, hart_idx++) {
			unsigned long hartid = 0;
			uint32_t irq = 0;

			if (fdt_hart_irq(fdt, node, hart_idx, &hartid, &irq))
				break;
			if (hart_index(hartid) >= 0) {
				files[hart_index(hartid)] =
					(uintptr_t)(base + off);
				mapped++;
			}
		}
	}
	return mapped;
}

static int imsic_probe(const void *fdt, int node)
{
	unsigned long val = 0;

	/* The supervisor-level files are the next stage's. */
	if (node < 0 || !irqchip_is_mlevel(fdt, node))
		return 0;
	/* No Smaia on the boot hart, no interrupt files to talk to. */
	if (!csr_probe(CSR_MISELECT, &val))
		return -1;
	if (imsic_map_files(fdt, node)) {
		ipi_register(&imsic_ipi_ops);
		pr_dbg("imsic: machine-level files carry the IPIs\n");
	}
	return 0;
}

static const char *const imsic_compatible[] = { "riscv,imsics", NULL };

DRIVER_DEFINE(imsic) = {
	.name = "imsic",
	.compatible = imsic_compatible,
	.probe = imsic_probe,
};
