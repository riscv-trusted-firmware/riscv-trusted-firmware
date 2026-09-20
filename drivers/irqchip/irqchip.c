// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * What the interrupt controller drivers share: telling machine-level
 * controllers from supervisor-level ones in the device tree, and the
 * fix-up that hides the former from the next stage.
 */

#include <arch/csr.h>
#include <fdt_util.h>
#include <irqchip.h>

#include "irqchip_internal.h"

static void (*hart_init)(void);

void irqchip_set_hart_init(void (*fn)(void))
{
	hart_init = fn;
}

void irqchip_hart_init(void)
{
	if (hart_init)
		hart_init();
}

/*
 * A controller is machine-level when it interrupts the harts through their
 * machine external interrupt: directly ("interrupts-extended"), or as MSIs
 * through an IMSIC that does ("msi-parent").
 */
bool irqchip_is_mlevel(const void *fdt, int node)
{
	unsigned long hartid = 0;
	uint32_t irq = 0;
	int parent = 0;

	if (!fdt_hart_irq(fdt, node, 0, &hartid, &irq))
		return irq == IRQ_M_EXT;

	parent = fdt_node_offset_by_phandle(fdt, fdt_prop_u32(fdt, node,
							      "msi-parent", 0));
	return parent >= 0 && parent != node && irqchip_is_mlevel(fdt, parent);
}

/* An OS that sees a machine-level APLIC or IMSIC tries to drive it. */
static int disable_mlevel(void *fdt, const char *compatible)
{
	int node = -1, rc = 0;

	while ((node = fdt_node_offset_by_compatible(fdt, node, compatible)) >=
	       0) {
		if (!irqchip_is_mlevel(fdt, node))
			continue;
		rc = fdt_node_disable(fdt, node);
		if (rc)
			return rc;
	}
	return 0;
}

bool irqchip_is_plic(const void *fdt, int node)
{
	return !fdt_node_check_compatible(fdt, node, "sifive,plic-1.0.0") ||
	       !fdt_node_check_compatible(fdt, node, "riscv,plic0");
}

/*
 * PLIC contexts of M-mode: marked invalid rather than removed, the index
 * counts.
 */
static int plic_invalidate_mlevel(void *fdt)
{
	int node = 0, len = 0;

	for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
	     node = fdt_next_node(fdt, node, NULL)) {
		fdt32_t *ext = NULL;

		if (!irqchip_is_plic(fdt, node))
			continue;
		ext = fdt_getprop_w(fdt, node, "interrupts-extended", &len);
		for (int i = 0; ext && i + 1 < len / 4; i += 2)
			if (fdt32_to_cpu(ext[i + 1]) == IRQ_M_EXT)
				ext[i + 1] = cpu_to_fdt32(0xffffffff);
	}
	return 0;
}

int irqchip_fdt_fixup(void *fdt)
{
	int rc = disable_mlevel(fdt, "riscv,aplic");

	if (!rc)
		rc = disable_mlevel(fdt, "riscv,imsics");
	if (!rc)
		rc = plic_invalidate_mlevel(fdt);
	return rc;
}
