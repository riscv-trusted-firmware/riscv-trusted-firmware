/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef MONITOR_FDT_FIXUP_H
#define MONITOR_FDT_FIXUP_H

/*
 * The device tree of the previous stage is read by the drivers, may be
 * completed by the platform, and is handed to the next stage with the
 * monitor's share of the machine taken out.
 *
 * fdt_prepare(), early: check the tree and make it writable with room to
 * grow, in place or at CONFIG_MONITOR_FDT_ADDR. Returns its address, or 0
 * when there is no usable tree (the next stage then gets what we got).
 *
 * fdt_fixup(), before the hand-over: reserve the monitor's memory, disable
 * the nodes that are the monitor's and the harts it does not manage.
 */
unsigned long fdt_prepare(unsigned long fdt);
void fdt_fixup(void *fdt);

#endif
