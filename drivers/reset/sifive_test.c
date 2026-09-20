// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SiFive test finisher ("sifive,test1"): the power-off / reboot device of
 * the QEMU virt and sifive_u machines.
 */

#include <driver.h>
#include <io.h>
#include <reset.h>
#include <stdint.h>
#include <types_ext.h>

#define FINISHER_PASS 0x5555
#define FINISHER_RESET 0x7777

static bool sifive_test_supported(enum reset_type type)
{
	return true;
}

static void sifive_test_reset(enum reset_type type)
{
	vaddr_t reg = CONFIG_RESET_SIFIVE_TEST_ADDR;

	io_write32(reg,
		   type == RESET_SHUTDOWN ? FINISHER_PASS : FINISHER_RESET);
}

static const struct reset_ops sifive_test_ops = {
	.name = "sifive-test",
	.supported = sifive_test_supported,
	.reset = sifive_test_reset,
};

static int sifive_test_probe(void)
{
	reset_register(&sifive_test_ops);
	return 0;
}

DRIVER_DEFINE(sifive_test) = {
	.name = "sifive-test",
	.probe = sifive_test_probe,
};
