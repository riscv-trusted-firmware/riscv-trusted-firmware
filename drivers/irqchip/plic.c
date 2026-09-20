// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V PLIC ("sifive,plic-1.0.0", "riscv,plic0"). The monitor takes no
 * external interrupts, so all there is to do is to start the next stage
 * from a quiet controller: every source at priority 0, every context
 * (M-mode and S-mode alike) with nothing enabled and the threshold at its
 * maximum. S-mode then programs its own contexts.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <log.h>
#include <types_ext.h>
#include <util.h>

#include "irqchip_internal.h"

#define PLIC_PRIORITY(src) (UL(4) * (src))
#define PLIC_ENABLE(ctx) (UL(0x2000) + UL(0x80) * (ctx))
#define PLIC_THRESHOLD(ctx) (UL(0x200000) + UL(0x1000) * (ctx))
#define PLIC_THRESHOLD_MAX 7

static void plic_init(vaddr_t base, uint32_t sources, unsigned int contexts)
{
	for (uint32_t src = 1; src <= sources; src++)
		io_write32(base + PLIC_PRIORITY(src), 0);
	for (unsigned int ctx = 0; ctx < contexts; ctx++) {
		for (uint32_t word = 0; word <= sources / 32; word++)
			io_write32(base + PLIC_ENABLE(ctx) + 4 * word, 0);
		io_write32(base + PLIC_THRESHOLD(ctx), PLIC_THRESHOLD_MAX);
	}
}

static int plic_probe(const void *fdt)
{
	int node = 0, len = 0;
	uint64_t base = 0;

	if (!fdt)
		return 0;
	for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
	     node = fdt_next_node(fdt, node, NULL)) {
		uint32_t sources = fdt_prop_u32(fdt, node, "riscv,ndev", 0);

		if (!irqchip_is_plic(fdt, node) ||
		    !fdt_node_enabled(fdt, node) ||
		    fdt_reg(fdt, node, 0, &base, NULL) ||
		    !fdt_getprop(fdt, node, "interrupts-extended", &len))
			continue;
		/* One context per (hart, interrupt) pair. */
		plic_init((uintptr_t)base, sources, (unsigned int)len / 8);
		pr_info("plic: %lx, %u sources, %d contexts\n",
			(unsigned long)base, sources, len / 8);
	}
	return 0;
}

DRIVER_DEFINE(plic) = {
	.name = "plic",
	.probe = plic_probe,
};
