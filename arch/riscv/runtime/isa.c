// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* The extensions the device tree lists, see <arch/isa.h>. */

#include <arch/hart.h>
#include <arch/isa.h>
#include <fdt_util.h>
#include <heap.h>
#include <libfdt.h>
#include <string.h>

/* The names, lower case, a space before and after each: " i m a zicbom ". */
static char *list;

static char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int boot_cpu_node(const void *fdt)
{
	int cpus = fdt_path_offset(fdt, "/cpus"), cpu = 0;
	uint64_t hartid = 0;

	if (cpus < 0)
		return -1;
	fdt_for_each_subnode(cpu, fdt, cpus)
		if (!fdt_reg(fdt, cpu, 0, &hartid, NULL) &&
		    hartid == this_hartid())
			return cpu;
	return -1;
}

void isa_init(const void *fdt)
{
	int cpu = fdt ? boot_cpu_node(fdt) : -1, len = 0;
	const char *prop = NULL;
	char *out = NULL;

	if (cpu < 0)
		return;
	prop = fdt_getprop(fdt, cpu, "riscv,isa-extensions", &len);
	if (prop && len > 0) {
		/* A string list: the separators become the spaces. */
		list = heap_alloc((size_t)len + 2);
		out = list;
		*out++ = ' ';
		for (int i = 0; i < len; i++)
			*out++ = prop[i] ? lower(prop[i]) : ' ';
		return;
	}

	/*
	 * "rv64imafdc_zicsr_zifencei": single letters, then names between
	 * underscores.
	 */
	prop = fdt_getprop(fdt, cpu, "riscv,isa", &len);
	if (!prop || len < 5 || lower(prop[0]) != 'r' || lower(prop[1]) != 'v')
		return;
	list = heap_alloc(2 * (size_t)len + 2);
	out = list;
	*out++ = ' ';
	for (prop += 4; *prop && *prop != '_'; prop++) {
		/*
		 * A multi-letter name without its underscore ends the single
		 * letters.
		 */
		if (strchr("sxz", lower(*prop)))
			break;
		*out++ = lower(*prop);
		*out++ = ' ';
	}
	for (; *prop; prop++)
		*out++ = *prop == '_' ? ' ' : lower(*prop);
	*out++ = ' ';
}

bool isa_known(void)
{
	return list;
}

bool isa_has(const char *ext)
{
	size_t n = strlen(ext);

	for (const char *p = list; p && (p = strstr(p, ext)); p += n)
		if (p[-1] == ' ' && (p[n] == ' ' || !p[n]))
			return true;
	return false;
}

const char *isa_string(void)
{
	return list ? list + 1 : NULL;
}
