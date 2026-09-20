/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef IRQCHIP_H
#define IRQCHIP_H

/*
 * External interrupt controllers. The monitor takes no wired interrupt
 * itself (an M-level IMSIC file carries its IPIs): its job is to leave the
 * controllers in a state the next stage can use them from S-mode
 * (machine-level contexts quiet, interrupt sources delegated) and to keep
 * the machine-level parts out of the next stage's device tree.
 */

#include <stdint.h>

/*
 * An interrupt of the monitor's own, as a message: whoever writes 'data'
 * (32 bits) to 'addr' has 'handler' run in M-mode on the hart that asked.
 * That takes machine-level interrupt files (an IMSIC); -1 without.
 *
 * The handler runs where the monitor looks at its IPIs: on a trap from
 * S-mode, and inside the monitor's own waits, locks held and all. It must
 * not wait for anything itself.
 */
struct irqchip_msi {
	uint64_t addr;
	uint32_t data;
};

#ifdef CONFIG_IRQCHIP_IMSIC
int irqchip_msi_request(void (*handler)(void *arg), void *arg,
			struct irqchip_msi *msi);
#else
static inline int irqchip_msi_request(void (*handler)(void *arg), void *arg,
				      struct irqchip_msi *msi)
{
	return -1;
}
#endif

#ifdef CONFIG_IRQCHIP

/* The device tree handed to the next stage. 0 or a libfdt error. */
int irqchip_fdt_fixup(void *fdt);

#else

static inline int irqchip_fdt_fixup(void *fdt)
{
	return 0;
}

#endif

#endif
