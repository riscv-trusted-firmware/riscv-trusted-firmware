/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/*
 * Entry points every image provides (called from arch/riscv/entry.S).
 * image_main runs on the boot hart with .bss cleared and a stack; the other
 * harts spin until boot_release_secondaries() and then run
 * image_secondary_main on their own stack.
 */
void image_main(unsigned long hartid, unsigned long fdt);
void image_secondary_main(unsigned long hartid);

extern uint32_t _boot_release;
/*
 * Harts that entered the image (always 0 without the A extension); READ_ONCE.
 */
extern uint32_t _boot_hart_count;
/* ... and those among them that found no place in the hart table. */
extern uint32_t _boot_hart_parked;

/*
 * The hart table: the ids of the harts the image manages. A hart's index
 * is its id's position in it, and what per-hart storage goes by: hart ids
 * can be sparse and large, indices are below CONFIG_PLATFORM_HART_COUNT.
 * The boot hart is index 0; the others follow in the order of the device
 * tree's /cpus (their ids counted from zero when there is no tree).
 *
 * boot_harts_init() must run before boot_release_secondaries(): entry.S
 * looks a released hart up here to give it a stack.
 */
extern unsigned long _boot_hart_ids[CONFIG_PLATFORM_HART_COUNT];
extern uint32_t _boot_hart_nr;

void boot_harts_init(const void *fdt, unsigned long boot_hartid);
/* -1: not a hart the image manages. */
int boot_hart_index(unsigned long hartid);

static inline void boot_release_secondaries(void)
{
	__asm__ __volatile__("fence rw, w" ::: "memory");
	_boot_release = 1;
}

#endif
