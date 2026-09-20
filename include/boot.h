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

static inline void boot_release_secondaries(void)
{
	__asm__ __volatile__("fence rw, w" ::: "memory");
	_boot_release = 1;
}

#endif
