// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V ACLINT MTIMER (and the timer half of the SiFive CLINT): one MTIME
 * counter, one MTIMECMP per hart, indexed by hart id minus the first hart
 * id of the device.
 */

#include <arch/hart.h>
#include <driver.h>
#include <io.h>
#include <memregion.h>
#include <timer.h>
#include <types_ext.h>
#include <util.h>

static const vaddr_t mtime = CONFIG_TIMER_ACLINT_MTIMER_MTIME_ADDR;

/* 0: this hart has none. */
static vaddr_t mtimecmp(void)
{
	unsigned long idx =
		this_hartid() - CONFIG_TIMER_ACLINT_MTIMER_FIRST_HART;

	return CONFIG_TIMER_ACLINT_MTIMER_MTIMECMP_ADDR + 8 * idx;
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

static int aclint_mtimer_probe(const void *fdt)
{
	timer_register(&aclint_mtimer_ops);
	/* One MTIMECMP per hart, and MTIME; S-mode has the time CSR. */
	memregion_add(CONFIG_TIMER_ACLINT_MTIMER_MTIMECMP_ADDR,
		      UL(8) * CONFIG_PLATFORM_HART_COUNT, MEMREGION_MMODE_RW);
	memregion_add(CONFIG_TIMER_ACLINT_MTIMER_MTIME_ADDR, 8,
		      MEMREGION_MMODE_RW);
	return 0;
}

DRIVER_DEFINE(aclint_mtimer) = {
	.name = "aclint-mtimer",
	.probe = aclint_mtimer_probe,
};
