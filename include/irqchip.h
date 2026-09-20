/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef IRQCHIP_H
#define IRQCHIP_H

/*
 * External interrupt controllers. The monitor takes no external interrupt
 * itself: its job is to leave the controllers in a state the next stage
 * can use them from S-mode (machine-level contexts quiet, interrupt
 * sources delegated) and to keep the machine-level parts out of the next
 * stage's device tree.
 */

#ifdef CONFIG_IRQCHIP

/* Every hart, before it enters the next stage. */
void irqchip_hart_init(void);
/* The device tree handed to the next stage. 0 or a libfdt error. */
int irqchip_fdt_fixup(void *fdt);

/* Internal: a controller with per-hart state registers this at probe time. */
void irqchip_set_hart_init(void (*fn)(void));

#else

static inline void irqchip_hart_init(void)
{
}

static inline int irqchip_fdt_fixup(void *fdt)
{
	return 0;
}

#endif

#endif
