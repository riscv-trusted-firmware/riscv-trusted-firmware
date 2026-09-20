/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef MONITOR_FDT_FIXUP_H
#define MONITOR_FDT_FIXUP_H

/*
 * Prepare the device tree of the previous stage for the next one: tell it
 * which memory the monitor keeps for itself. Returns the address to pass
 * on, which is 'fdt' itself unless CONFIG_MONITOR_FDT_ADDR relocates it.
 * A missing or broken tree is passed on untouched.
 */
unsigned long fdt_fixup(unsigned long fdt);

#endif
