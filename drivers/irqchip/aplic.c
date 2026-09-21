// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V APLIC ("riscv,aplic"), machine-level (root) interrupt domain.
 *
 * Only M-mode can program the root domain, and S-mode gets no wired
 * interrupt until it has: the sources named by "riscv,delegate" are
 * delegated to the child domain the next stage will drive, and in MSI
 * mode the address of the IMSIC files, which a supervisor-level domain can
 * only read, is set for both levels. The root domain itself stays off:
 * the monitor takes no external interrupts.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <log.h>
#include <memregion.h>
#include <types_ext.h>
#include <util.h>

#include "irqchip_internal.h"

#define APLIC_DOMAINCFG 0x0000
#define APLIC_SOURCECFG(src) (0x0004 + UL(4) * ((src) - 1))
#define APLIC_SOURCECFG_D BIT(10)
#define APLIC_MMSIADDRCFG 0x1bc0
#define APLIC_SMSIADDRCFG 0x1bc8
#define APLIC_CLRIE(word) (0x1f00 + UL(4) * (word))
#define APLIC_TARGET(src) (0x3004 + UL(4) * ((src) - 1))
#define APLIC_IDC(idx) (0x4000 + UL(32) * (idx))
#define APLIC_IDC_IDELIVERY 0x00
#define APLIC_IDC_IFORCE 0x04
#define APLIC_IDC_ITHRESHOLD 0x08

#define IMSIC_PAGE_SHIFT 12

/* An IMSIC's place in the address map, as the xMSIADDRCFG registers want it. */
struct msicfg {
	uint64_t base;
	uint32_t lhxs, lhxw, hhxs, hhxw;
};

static uint32_t log2_ceil(uint32_t n)
{
	uint32_t bits = 0;

	while (BIT32(bits) < n)
		bits++;
	return bits;
}

static bool msicfg_from_imsic(const void *fdt, int imsic, struct msicfg *cfg)
{
	int len = 0;

	if (imsic < 0 || fdt_reg(fdt, imsic, 0, &cfg->base, NULL) ||
	    !fdt_getprop(fdt, imsic, "interrupts-extended", &len))
		return false;

	cfg->lhxs = fdt_prop_u32(fdt, imsic, "riscv,guest-index-bits", 0);
	cfg->lhxw = fdt_prop_u32(fdt, imsic, "riscv,hart-index-bits",
				 log2_ceil((uint32_t)len / 8));
	cfg->hhxw = fdt_prop_u32(fdt, imsic, "riscv,group-index-bits", 0);
	cfg->hhxs = fdt_prop_u32(fdt, imsic, "riscv,group-index-shift",
				 2 * IMSIC_PAGE_SHIFT) -
		    2 * IMSIC_PAGE_SHIFT;
	return true;
}

static void msicfg_write(vaddr_t base, unsigned long off,
			 const struct msicfg *cfg)
{
	uint64_t ppn = cfg->base >> IMSIC_PAGE_SHIFT;

	/* The base is that of hart 0, guest 0, group 0. */
	ppn &= ~(BIT64(cfg->lhxs) - 1);
	ppn &= ~((BIT64(cfg->lhxw) - 1) << cfg->lhxs);
	ppn &= ~((BIT64(cfg->hhxw) - 1) << (cfg->hhxs + IMSIC_PAGE_SHIFT));

	io_write32(base + off, (uint32_t)ppn);
	io_write32(base + off + 4,
		   (high32_from_64(ppn) & 0xfff) | (cfg->lhxw << 12) |
		   (cfg->hhxw << 16) | (cfg->lhxs << 20) |
		   (cfg->hhxs << 24));
}

/* Index of the child domain 'phandle' in "riscv,children", -1 if it is none. */
static int child_index(const void *fdt, int node, uint32_t phandle)
{
	int len = 0;
	const fdt32_t *children =
		fdt_getprop(fdt, node, "riscv,children", &len);

	for (int i = 0; children && i < len / 4; i++)
		if (fdt32_to_cpu(children[i]) == phandle)
			return i;
	return -1;
}

static void aplic_root_init(const void *fdt, int node, vaddr_t base,
			    uint32_t sources)
{
	const fdt32_t *deleg = NULL;
	struct msicfg cfg = {};
	unsigned int idcs = 0;
	uint32_t phandle = 0;
	int len = 0, child = 0, imsic = 0;

	io_write32(base + APLIC_DOMAINCFG, 0);
	for (uint32_t word = 0; word <= sources / 32; word++)
		io_write32(base + APLIC_CLRIE(word), ~U(0));
	for (uint32_t src = 1; src <= sources; src++) {
		io_write32(base + APLIC_SOURCECFG(src), 0);
		/* hart 0, lowest priority */
		io_write32(base + APLIC_TARGET(src), 1);
	}

	/* <child domain, first source, last source>, under its old name too. */
	deleg = fdt_getprop(fdt, node, "riscv,delegate", &len);
	if (!deleg)
		deleg = fdt_getprop(fdt, node, "riscv,delegation", &len);
	for (int i = 0; deleg && i + 2 < len / 4; i += 3) {
		uint32_t first = fdt32_to_cpu(deleg[i + 1]);
		uint32_t last = fdt32_to_cpu(deleg[i + 2]);
		int idx = child_index(fdt, node, fdt32_to_cpu(deleg[i]));

		if (idx < 0 || !first || first > last || last > sources)
			continue;
		for (uint32_t src = first; src <= last; src++)
			io_write32(base + APLIC_SOURCECFG(src),
				   APLIC_SOURCECFG_D | (uint32_t)idx);
	}

	/*
	 * Direct mode: one interrupt delivery control block per hart, all off.
	 */
	if (fdt_getprop(fdt, node, "interrupts-extended", &len))
		idcs = (unsigned int)len / 8;
	for (unsigned int i = 0; i < idcs; i++) {
		io_write32(base + APLIC_IDC(i) + APLIC_IDC_IDELIVERY, 0);
		io_write32(base + APLIC_IDC(i) + APLIC_IDC_IFORCE, 0);
		io_write32(base + APLIC_IDC(i) + APLIC_IDC_ITHRESHOLD, 1);
	}

	/* MSI mode: our IMSIC, and the one of the (first) child domain. */
	phandle = fdt_prop_u32(fdt, node, "msi-parent", 0);
	imsic = fdt_node_offset_by_phandle(fdt, phandle);
	if (msicfg_from_imsic(fdt, imsic, &cfg)) {
		cfg.lhxs = 0; /* no guest files at machine level */
		msicfg_write(base, APLIC_MMSIADDRCFG, &cfg);
	}
	phandle = fdt_prop_u32(fdt, node, "riscv,children", 0);
	child = fdt_node_offset_by_phandle(fdt, phandle);
	imsic = -1;
	if (child >= 0) {
		phandle = fdt_prop_u32(fdt, child, "msi-parent", 0);
		imsic = fdt_node_offset_by_phandle(fdt, phandle);
	}
	if (child >= 0 && msicfg_from_imsic(fdt, imsic, &cfg))
		msicfg_write(base, APLIC_SMSIADDRCFG, &cfg);

	pr_info("aplic: %lx, %u sources, %s mode, delegated to S-mode\n",
		(unsigned long)base, sources, idcs ? "direct" : "MSI");
}

static int aplic_probe(const void *fdt, int node)
{
	uint64_t base = 0, size = 0;

	/* The supervisor-level domains are the next stage's. */
	if (node < 0 || !irqchip_is_mlevel(fdt, node))
		return 0;
	if (fdt_reg(fdt, node, 0, &base, &size))
		return -1;
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_MMODE_RW);
	aplic_root_init(fdt, node, (vaddr_t)base,
			MIN(fdt_prop_u32(fdt, node, "riscv,num-sources", 0),
			    U(1023)));
	return 0;
}

static const char *const aplic_compatible[] = { "riscv,aplic", NULL };

DRIVER_DEFINE(aplic) = {
	.name = "aplic",
	.compatible = aplic_compatible,
	.probe = aplic_probe,
};
