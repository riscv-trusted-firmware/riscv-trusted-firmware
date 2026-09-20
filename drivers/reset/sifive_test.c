// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SiFive test finisher ("sifive,test1", "sifive,test0"): the power-off /
 * reboot device of the QEMU virt and sifive_u machines.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <memregion.h>
#include <reset.h>
#include <stdint.h>
#include <types_ext.h>

#define FINISHER_PASS 0x5555
#define FINISHER_RESET 0x7777

static vaddr_t finisher;

static bool sifive_test_supported(enum reset_type type)
{
	return true;
}

static void sifive_test_reset(enum reset_type type)
{
	io_write32(finisher,
		   type == RESET_SHUTDOWN ? FINISHER_PASS : FINISHER_RESET);
}

static const struct reset_ops sifive_test_ops = {
	.name = "sifive-test",
	.rating = 100,
	.supported = sifive_test_supported,
	.reset = sifive_test_reset,
};

static int sifive_test_probe(const void *fdt, int node)
{
	uint64_t base = CONFIG_RESET_SIFIVE_TEST_ADDR, size = 0x1000;

	if (finisher || (node < 0 && !base))
		return 0;
	if (node >= 0 && fdt_reg(fdt, node, 0, &base, &size))
		return -1;

	finisher = (vaddr_t)base;
	reset_register(&sifive_test_ops);
	/* Operating systems know the device too (syscon-poweroff). */
	memregion_add((unsigned long)base, (unsigned long)size,
		      MEMREGION_SHARED_RW);
	return 0;
}

static const char *const sifive_test_compatible[] = {
	"sifive,test1",
	"sifive,test0",
	NULL,
};

DRIVER_DEFINE(sifive_test) = {
	.name = "sifive-test",
	.compatible = sifive_test_compatible,
	.probe_without_node = true,
	.probe = sifive_test_probe,
};
