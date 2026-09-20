// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V ACLINT MTIMER ("riscv,aclint-mtimer") and the timer half of the
 * SiFive CLINT ("riscv,clint0", "sifive,clint0"): one MTIME counter, and
 * one MTIMECMP per hart, numbered by the hart's position among the machine
 * timer interrupts the node lists. Without a node the Kconfig geometry
 * applies, with hart ids counted from TIMER_ACLINT_MTIMER_FIRST_HART.
 */

#include <arch/hart.h>
#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <memregion.h>
#include <timer.h>
#include <types_ext.h>
#include <util.h>

#define CLINT_MTIMECMP_OFFSET 0x4000
#define CLINT_MTIME_OFFSET 0xbff8

static vaddr_t mtime;
static vaddr_t mtimecmp_base;
/* Which MTIMECMP is a hart's, -1: none. */
static int mtimecmp_index[CONFIG_PLATFORM_HART_COUNT];

/* 0: this hart has none. */
static vaddr_t mtimecmp(void)
{
	int idx = mtimecmp_index[this_hartid()];

	return idx < 0 ? 0 : mtimecmp_base + 8 * (vaddr_t)idx;
}

/* 32-bit accesses on every XLEN: not all implementations take 64-bit ones. */
static uint64_t aclint_mtimer_now(void)
{
	uint32_t hi = 0, lo = 0;

	do {
		hi = io_read32(mtime + 4);
		lo = io_read32(mtime);
	} while (hi != io_read32(mtime + 4));
	return reg_pair_to_64(hi, lo);
}

static void aclint_mtimer_set_event(uint64_t when)
{
	vaddr_t cmp = mtimecmp();

	if (!cmp)
		return;
	/* Keep the compare value in the future while it is half-written. */
	io_write32(cmp, ~U(0));
	io_write32(cmp + 4, high32_from_64(when));
	io_write32(cmp, (uint32_t)when);
}

static void aclint_mtimer_stop_event(void)
{
	aclint_mtimer_set_event(~ULL(0));
}

static const struct timer_ops aclint_mtimer_ops = {
	.name = "aclint-mtimer",
	.now = aclint_mtimer_now,
	.set_event = aclint_mtimer_set_event,
	.stop_event = aclint_mtimer_stop_event,
};

static int aclint_mtimer_probe(const void *fdt, int node)
{
	uint64_t mtime_addr = 0, cmp_addr = 0;
	unsigned int harts = 0;

	if (mtime)
		return 0; /* one time base is all the monitor uses */

	if (node < 0) {
		mtime_addr = CONFIG_TIMER_ACLINT_MTIMER_MTIME_ADDR;
		cmp_addr = CONFIG_TIMER_ACLINT_MTIMER_MTIMECMP_ADDR;
		harts = CONFIG_PLATFORM_HART_COUNT;
		for (unsigned int h = 0; h < harts; h++)
			mtimecmp_index[h] =
				(int)h - CONFIG_TIMER_ACLINT_MTIMER_FIRST_HART;
	} else {
		if (!fdt_node_check_compatible(fdt, node,
					       "riscv,aclint-mtimer")) {
			/* "reg": MTIME, then the MTIMECMP array. */
			if (fdt_reg(fdt, node, 0, &mtime_addr, NULL) ||
			    fdt_reg(fdt, node, 1, &cmp_addr, NULL))
				return -1;
		} else {
			if (fdt_reg(fdt, node, 0, &cmp_addr, NULL))
				return -1;
			mtime_addr = cmp_addr + CLINT_MTIME_OFFSET;
			cmp_addr += CLINT_MTIMECMP_OFFSET;
		}
		harts = fdt_hart_indices(fdt, node, IRQ_M_TIMER, mtimecmp_index,
					 CONFIG_PLATFORM_HART_COUNT);
		if (!harts)
			return -1;
		timer_frequency_from_fdt(fdt);
	}

	mtime = (vaddr_t)mtime_addr;
	mtimecmp_base = (vaddr_t)cmp_addr;
	timer_register(&aclint_mtimer_ops);
	/* S-mode has the time CSR: these registers are the monitor's. */
	memregion_add(mtimecmp_base, UL(8) * CONFIG_PLATFORM_HART_COUNT,
		      MEMREGION_MMODE_RW);
	memregion_add((unsigned long)mtime_addr, 8, MEMREGION_MMODE_RW);
	return 0;
}

static const char *const aclint_mtimer_compatible[] = {
	"riscv,aclint-mtimer",
	"riscv,clint0",
	"sifive,clint0",
	NULL,
};

DRIVER_DEFINE(aclint_mtimer) = {
	.name = "aclint-mtimer",
	.compatible = aclint_mtimer_compatible,
	.probe_without_node = true,
	.probe = aclint_mtimer_probe,
};
