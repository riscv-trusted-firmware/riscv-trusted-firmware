// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * First-stage loader: bring up the console, release the other harts and
 * jump every hart to the monitor with (a0 = hart id, a1 = fdt).
 */

#include <boot.h>
#include <generated/version.h>
#include <log.h>
#include <platform.h>

typedef void (*next_stage_fn)(unsigned long hartid, unsigned long fdt);

static unsigned long saved_fdt;

static void __noreturn jump_next(unsigned long hartid)
{
	((next_stage_fn)CONFIG_LOADER_NEXT_STAGE_ADDR)(hartid, saved_fdt);
	for (;;)
		;
}

void image_main(unsigned long hartid, unsigned long fdt)
{
	plat_early_init();
	pr_info("\n%s loader %s: hart %lu -> %lx\n", PROJECT_NAME,
		PROJECT_VERSION, hartid,
		(unsigned long)CONFIG_LOADER_NEXT_STAGE_ADDR);

	saved_fdt = fdt;
	boot_release_secondaries();
	jump_next(hartid);
}

void image_secondary_main(unsigned long hartid)
{
	jump_next(hartid);
}
