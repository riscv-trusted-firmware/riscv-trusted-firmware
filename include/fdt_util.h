/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef FDT_UTIL_H
#define FDT_UTIL_H

/* Device tree helpers on top of libfdt. Return values are libfdt's. */

#include <libfdt.h>
#include <stdbool.h>

/* Every node of the tree, in order. */
/* 'fdt' if it is a device tree that holds up, NULL otherwise. */
const void *fdt_valid(const void *fdt);

/* Does the node list one of the NULL-terminated 'compatible' strings? */
bool fdt_node_compatible_any(const void *fdt, int node,
			     const char *const *compatible);

/* No "status", or "okay" / "ok". */
bool fdt_node_enabled(const void *fdt, int node);
int fdt_node_disable(void *fdt, int node);

/* Entry 'index' of "reg", decoded with the parent's #address/#size-cells. */
int fdt_reg(const void *fdt, int node, int index, uint64_t *addr,
	    uint64_t *size);

/* The "reg" entry that "reg-names" calls 'name'. */
int fdt_reg_by_name(const void *fdt, int node, const char *name, uint64_t *addr,
		    uint64_t *size);

/* Is [base, base + size) part of what a "memory" node describes as RAM? */
bool fdt_range_is_memory(const void *fdt, uint64_t base, uint64_t size);

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

/*
 * Devices with one register (set) per hart number them by position among
 * the "interrupts-extended" entries for local interrupt 'irq': fill
 * index_of[hartid] for the harts below 'nr_harts', -1 where the device has
 * nothing for a hart. Returns how many harts it has something for.
 */
unsigned int fdt_hart_indices(const void *fdt, int node, uint32_t irq,
			      int *index_of, unsigned int nr_harts);

#endif
