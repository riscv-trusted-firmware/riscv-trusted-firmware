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
#include <arch/isa.h>
#include <arch/pmu.h>
#include <boot.h>
#include <domain.h>
#include <driver.h>
#include <fdt_util.h>
#include <generated/version.h>
#include <heap.h>
#include <handover.h>
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
		[HART_FEAT_SMCNTRPMF] = "smcntrpmf",
		[HART_FEAT_SMCDELEG] = "smcdeleg",
		[HART_FEAT_ZKR] = "zkr",
		[HART_FEAT_AIA] = "aia",
		[HART_FEAT_RAS_IRQ] = "ras-irq",
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

/* What the previous stage says about the next one, where it says anything. */
static void handover_apply(const struct boot_handover *h, unsigned long *next,
			   unsigned long *mode)
{
	if (!h)
		return;
	log_quiet = h->options & BOOT_HANDOVER_OPT_QUIET;
	/* A block without an address: QEMU without -kernel, for one. */
	if (!h->next_addr)
		return;
	*next = h->next_addr;
	if (h->next_mode == BOOT_HANDOVER_MODE_S ||
	    h->next_mode == BOOT_HANDOVER_MODE_U)
		*mode = h->next_mode;
}

void image_main(unsigned long hartid, unsigned long fdt,
		const struct boot_handover *handover)
{
	unsigned long next = CONFIG_MONITOR_NEXT_STAGE_ADDR, mode = PRV_S;
	void *tree = NULL;

	handover_apply(handover, &next, &mode);

	/* The hart table first: it is what gives a hart its per-hart state. */
	boot_harts_init(fdt_valid((const void *)fdt), hartid);
	/*
	 * The harts have their stacks; the rest of the monitor's memory is the
	 * heap.
	 */
	heap_init(_boot_hart_nr);
	hart_init(hartid);
	plat_early_init((const void *)fdt);

	pr_info("\n%s %s\n", PROJECT_NAME, PROJECT_VERSION);
	pr_info("platform: %s, target: %s, boot hart: %lu, fdt: %lx\n",
		CONFIG_PLATFORM_NAME, BUILD_TARGET, hartid, fdt);
	if (handover) {
		pr_info("hand-over: next stage at %lx, mode %lu, options %lx (version %lu)\n",
			handover->next_addr, handover->next_mode,
			handover->options, handover->version);
		if (handover->next_addr && mode != handover->next_mode)
			pr_warn("hand-over: mode %lu is not for a next stage of the monitor, S-mode it is\n",
				handover->next_mode);
	}
	pr_info("misa: %lx mvendorid: %lx marchid: %lx mimpid: %lx\n",
		csr_read(misa), csr_read(mvendorid), csr_read(marchid),
		csr_read(mimpid));

	isa_init(fdt_valid((const void *)fdt));
	hart_detect_features();
	/* From here on the tree is ours to read, complete and cut down. */
	tree = (void *)fdt_prepare(fdt);
	if (tree && plat_fdt_prepare(tree))
		pr_warn("platform: could not complete the device tree\n");
#ifdef CONFIG_DOMAINS
	domains_init(tree, next, mode);
	domain_contexts_init();
#endif
	/*
	 * How many harts and domains there are is known: what is kept for each
	 * of them.
	 */
	hart_services_init();
	drivers_init(tree);
	services_init();
	plat_init();

	boot_release_secondaries();
	wait_for_secondaries();

	print_features();
	if (isa_string())
		pr_info("isa: %s\n", isa_string());
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
	pr_info("memory: %lu KiB of %lu free\n",
		(unsigned long)heap_free_bytes() >> 10,
		(unsigned long)CONFIG_MONITOR_SIZE >> 10);
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
#ifdef CONFIG_DOMAINS
	domains_start(fdt);
#else
	pr_info("monitor: next stage at %lx (%c-mode), fdt: %lx\n", next,
		mode == PRV_S ? 'S' : 'U', fdt);
	/*
	 * A boot hart without S-mode has done its part: the first hart with one
	 * goes on.
	 */
	for (unsigned int i = 0; this_hart()->no_smode && hart_by_index(i); i++)
		if (hart_index_valid(i) && !hsm_hart_boot(i, next, fdt, mode))
			hsm_hart_wait();
	hsm_boot_hart_start(next, fdt, mode);
#endif
}

void image_secondary_main(unsigned long hartid)
{
	hart_init(hartid);
	hsm_hart_wait();
}
