// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * First-stage loader: bring up the console, release the other harts and
 * jump every hart to the monitor with (a0 = hart id, a1 = fdt, a2 = the
 * hand-over block the loader was given itself: what it says is about the
 * stage after the monitor, which is the monitor's business).
 */

#include <boot.h>
#include <fdt_util.h>
#include <generated/version.h>
#include <log.h>
#include <platform.h>

typedef void (*next_stage_fn)(unsigned long hartid, unsigned long fdt,
			      const struct boot_handover *handover);

static unsigned long saved_fdt;
static const struct boot_handover *saved_handover;

static void __noreturn jump_next(unsigned long hartid)
{
	((next_stage_fn)CONFIG_LOADER_NEXT_STAGE_ADDR)(hartid, saved_fdt,
						       saved_handover);
	for (;;)
		;
}

void image_main(unsigned long hartid, unsigned long fdt,
		const struct boot_handover *handover)
{
	plat_early_init((const void *)fdt);
	pr_info("\n%s loader %s: hart %lu -> %lx\n", PROJECT_NAME,
		PROJECT_VERSION, hartid,
		(unsigned long)CONFIG_LOADER_NEXT_STAGE_ADDR);

	saved_fdt = fdt;
	saved_handover = handover;
	boot_harts_init(fdt_valid((const void *)fdt), hartid);
	boot_release_secondaries();
	jump_next(hartid);
}

void image_secondary_main(unsigned long hartid)
{
	jump_next(hartid);
}
