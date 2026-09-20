// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <fdt_util.h>
#include <string.h>

bool fdt_node_enabled(const void *fdt, int node)
{
	const char *status = fdt_getprop(fdt, node, "status", NULL);

	return !status || !strcmp(status, "okay") || !strcmp(status, "ok");
}

int fdt_node_disable(void *fdt, int node)
{
	static const char disabled[] = "disabled";

	return fdt_setprop(fdt, node, "status", disabled, sizeof(disabled));
}

static uint64_t read_cells(const fdt32_t *p, int cells)
{
	uint64_t val = 0;

	for (int i = 0; i < cells; i++)
		val = (val << 32) | fdt32_to_cpu(p[i]);
	return val;
}

int fdt_reg(const void *fdt, int node, int index, uint64_t *addr,
	    uint64_t *size)
{
	int parent = fdt_parent_offset(fdt, node), ac = 0, sc = 0, len = 0;
	const fdt32_t *reg = NULL;

	if (parent < 0)
		return parent;
	ac = fdt_address_cells(fdt, parent);
	sc = fdt_size_cells(fdt, parent);
	if (ac < 1 || ac > 2 || sc < 0 || sc > 2)
		return -FDT_ERR_BADNCELLS;

	reg = fdt_getprop(fdt, node, "reg", &len);
	if (!reg)
		return len;
	if ((index + 1) * (ac + sc) * 4 > len)
		return -FDT_ERR_NOTFOUND;
	reg += index * (ac + sc);
	*addr = read_cells(reg, ac);
	if (size)
		*size = read_cells(reg + ac, sc);
	return 0;
}

uint32_t fdt_prop_u32(const void *fdt, int node, const char *name,
		      uint32_t dflt)
{
	int len = 0;
	const fdt32_t *val = fdt_getprop(fdt, node, name, &len);

	return val && len >= 4 ? fdt32_to_cpu(*val) : dflt;
}

int fdt_hart_irq(const void *fdt, int node, int index, unsigned long *hartid,
		 uint32_t *irq)
{
	int len = 0, intc = 0, cpu = 0;
	const fdt32_t *ext =
		fdt_getprop(fdt, node, "interrupts-extended", &len);
	uint64_t id = 0;

	if (!ext)
		return len;
	if ((index + 1) * 8 > len)
		return -FDT_ERR_NOTFOUND;

	/* The cpu-intc is a child of its hart's cpu node. */
	intc = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(ext[2 * index]));
	if (intc < 0)
		return intc;
	cpu = fdt_parent_offset(fdt, intc);
	if (cpu < 0)
		return cpu;
	if (fdt_reg(fdt, cpu, 0, &id, NULL))
		return -FDT_ERR_BADVALUE;

	*hartid = (unsigned long)id;
	*irq = fdt32_to_cpu(ext[2 * index + 1]);
	return 0;
}

const void *fdt_valid(const void *fdt)
{
	return fdt && !fdt_check_header(fdt) ? fdt : NULL;
}

bool fdt_node_compatible_any(const void *fdt, int node,
			     const char *const *compatible)
{
	for (; *compatible; compatible++)
		if (!fdt_node_check_compatible(fdt, node, *compatible))
			return true;
	return false;
}

int fdt_reg_by_name(const void *fdt, int node, const char *name, uint64_t *addr,
		    uint64_t *size)
{
	int index = fdt_stringlist_search(fdt, node, "reg-names", name);

	return index < 0 ? index : fdt_reg(fdt, node, index, addr, size);
}

bool fdt_range_is_memory(const void *fdt, uint64_t base, uint64_t size)
{
	uint64_t start = 0, len = 0;
	int node = 0;

	for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
	     node = fdt_next_node(fdt, node, NULL)) {
		const char *type = fdt_getprop(fdt, node, "device_type", NULL);

		if (!type || strcmp(type, "memory"))
			continue;
		for (int i = 0; !fdt_reg(fdt, node, i, &start, &len); i++)
			if (base >= start && base + size <= start + len)
				return true;
	}
	return false;
}

unsigned int fdt_hart_indices(const void *fdt, int node, uint32_t irq,
			      int *index_of, unsigned int nr_harts)
{
	unsigned int found = 0;
	unsigned long hartid = 0;
	uint32_t this_irq = 0;
	int index = 0;

	for (unsigned int h = 0; h < nr_harts; h++)
		index_of[h] = -1;
	for (int i = 0; !fdt_hart_irq(fdt, node, i, &hartid, &this_irq); i++) {
		if (this_irq != irq)
			continue;
		if (hartid < nr_harts) {
			index_of[hartid] = index;
			found++;
		}
		index++;
	}
	return found;
}
