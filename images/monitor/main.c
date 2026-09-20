// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * M-mode runtime monitor: boot sequence.
 *
 * The boot hart brings up the console, drivers, services and the platform,
 * releases the other harts and enters the next stage in S-mode. Every other
 * hart parks in the HSM wait loop until the next stage starts it.
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/pmu.h>
#include <boot.h>
#include <driver.h>
#include <fdt_util.h>
#include <generated/version.h>
#include <io.h>
#include <ipi.h>
#include <log.h>
#include <mpxy.h>
#include <platform.h>
#include <reset.h>
#ifdef CONFIG_RPMI
#include <rpmi.h>
#endif
#include <service.h>
#include <suspend.h>
#include <timer.h>

#include "fdt_fixup.h"

static void print_features(void)
{
	static const char *const names[HART_FEAT_COUNT] = {
		[HART_FEAT_PMP] = "pmp",
		[HART_FEAT_SMEPMP] = "smepmp",
		[HART_FEAT_TIME_CSR] = "time",
		[HART_FEAT_MENVCFG] = "menvcfg",
		[HART_FEAT_SSTC] = "sstc",
		[HART_FEAT_SSCOFPMF] = "sscofpmf",
		[HART_FEAT_SMSTATEEN] = "smstateen",
		[HART_FEAT_SDTRIG] = "sdtrig",
		[HART_FEAT_SSDBLTRP] = "ssdbltrp",
		[HART_FEAT_H] = "h",
	};

	pr_info("features:");
	for (unsigned int i = 0; i < HART_FEAT_COUNT; i++)
		if (hart_has(i))
			pr_info(" %s", names[i]);
	pr_info("\n");
}

static void print_services(void)
{
	const struct service *s = NULL;

	pr_info("services:");
	for_each_service(s)
		if (service_probe(s->eid_min))
			pr_info(" %s", s->name);
	pr_info("\n");
}

/* Every hart that entered the image must be known before harts are managed. */
static void wait_for_secondaries(void)
{
	while (hart_count() + READ_ONCE(_boot_hart_parked) <
	       READ_ONCE(_boot_hart_count))
		cpu_relax();
}

void image_main(unsigned long hartid, unsigned long fdt)
{
	unsigned long next = CONFIG_MONITOR_NEXT_STAGE_ADDR;
	void *tree = NULL;

	/* The hart table first: it is what gives a hart its per-hart state. */
	boot_harts_init(fdt_valid((const void *)fdt), hartid);
	hart_init(hartid);
	plat_early_init((const void *)fdt);

	pr_info("\n%s %s\n", PROJECT_NAME, PROJECT_VERSION);
	pr_info("platform: %s, target: %s, boot hart: %lu, fdt: %lx\n",
		CONFIG_PLATFORM_NAME, BUILD_TARGET, hartid, fdt);
	pr_info("misa: %lx mvendorid: %lx marchid: %lx mimpid: %lx\n",
		csr_read(misa), csr_read(mvendorid), csr_read(marchid),
		csr_read(mimpid));

	hart_detect_features();
	/* From here on the tree is ours to read, complete and cut down. */
	tree = (void *)fdt_prepare(fdt);
	if (tree && plat_fdt_prepare(tree))
		pr_warn("platform: could not complete the device tree\n");
	drivers_init(tree);
	services_init();
	plat_init();

	boot_release_secondaries();
	wait_for_secondaries();

	print_features();
	pr_info("timer: %s, ipi: %s, reset: %s, harts: %u\n", timer_name(),
		ipi_name(), reset_name(), hart_count());
#ifdef CONFIG_RPMI
	pr_info("rpmi: %u transport(s), hart power: %s, suspend: %s\n",
		rpmi_context_count(), hsm_name(), suspend_name());
#endif
#ifdef CONFIG_MPXY
	pr_info("mpxy: %u channel(s)\n", mpxy_channel_count());
#endif
	print_services();
	pmu_init(tree);

	/* All-zero is no RISC-V instruction: nothing was loaded there. */
	if (*(const uint32_t *)next == 0) {
		pr_info("monitor: idle (no next stage at %lx)\n", next);
		hsm_hart_wait();
	}

	if (tree) {
		fdt_fixup(tree);
		fdt = (unsigned long)tree;
	}
	pr_info("monitor: next stage at %lx (S-mode), fdt: %lx\n", next, fdt);
	hsm_boot_hart_start(next, fdt);
}

void image_secondary_main(unsigned long hartid)
{
	hart_init(hartid);
	hsm_hart_wait();
}
