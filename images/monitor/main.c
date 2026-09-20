// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * M-mode runtime monitor: boot sequence.
 */

#include <arch/csr.h>
#include <boot.h>
#include <driver.h>
#include <generated/version.h>
#include <log.h>
#include <platform.h>
#include <service.h>

void image_main(unsigned long hartid, unsigned long fdt)
{
	plat_early_init();

	pr_info("\n%s %s\n", PROJECT_NAME, PROJECT_VERSION);
	pr_info("platform: %s, target: %s, boot hart: %lu, fdt: %lx\n",
		CONFIG_PLATFORM_NAME, BUILD_TARGET, hartid, fdt);
	pr_info("misa: %lx mvendorid: %lx marchid: %lx mimpid: %lx\n",
		csr_read(misa), csr_read(mvendorid), csr_read(marchid),
		csr_read(mimpid));

	drivers_init();
	services_init();
	plat_init();

	boot_release_secondaries();

	pr_info("monitor: idle (no next stage yet)\n");
	for (;;)
		wfi();
}

void image_secondary_main(unsigned long hartid)
{
	for (;;)
		wfi();
}
