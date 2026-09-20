/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef FDT_UTIL_H
#define FDT_UTIL_H

/* Device tree helpers on top of libfdt. Return values are libfdt's. */

#include <libfdt.h>
#include <stdbool.h>

/* No "status", or "okay" / "ok". */
bool fdt_node_enabled(const void *fdt, int node);
int fdt_node_disable(void *fdt, int node);

/* Entry 'index' of "reg", decoded with the parent's #address/#size-cells. */
int fdt_reg(const void *fdt, int node, int index, uint64_t *addr,
	    uint64_t *size);

uint32_t fdt_prop_u32(const void *fdt, int node, const char *name,
		      uint32_t dflt);

/*
 * "interrupts-extended" of an interrupt controller wired to the harts'
 * local interrupt controllers: pairs of (phandle of a "riscv,cpu-intc",
 * local interrupt number). Entry 'index' as (hart id, interrupt number);
 * -FDT_ERR_NOTFOUND past the end.
 */
int fdt_hart_irq(const void *fdt, int node, int index, unsigned long *hartid,
		 uint32_t *irq);

#endif
