/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef IRQCHIP_INTERNAL_H
#define IRQCHIP_INTERNAL_H

#include <stdbool.h>

bool irqchip_is_mlevel(const void *fdt, int node);
bool irqchip_is_plic(const void *fdt, int node);

/* Every node of the tree, in order. */
#endif
