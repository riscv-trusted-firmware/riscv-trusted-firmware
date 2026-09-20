/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef HANDOVER_H
#define HANDOVER_H

/*
 * What the previous boot stage may say about the next one.
 *
 * Where the monitor's next stage is loaded is often only known to whoever
 * loaded it. That stage passes a small block in a2, next to the hart id in
 * a0 and the device tree in a1: where the next stage is, the mode to enter
 * it in, and optionally which hart is to boot. The layout is the one boot
 * loaders for RISC-V already produce for their M-mode firmware (U-Boot SPL,
 * EDK2, coreboot, QEMU's -kernel, which know it as the firmware's "dynamic
 * information"), so that the monitor drops in where they expect one; every
 * field is an unsigned long.
 *
 * Without a block, or with a next address of zero in it, the monitor goes
 * by its configuration (CONFIG_MONITOR_NEXT_STAGE_ADDR) or, for domains,
 * by the device tree.
 */

#define BOOT_HANDOVER_MAGIC 0x4942534f
/* Version 2 added boot_hart. */
#define BOOT_HANDOVER_VERSION 2

#define BOOT_HANDOVER_MODE_U 0
#define BOOT_HANDOVER_MODE_S 1
#define BOOT_HANDOVER_MODE_M 3

/* Keep quiet on the console while booting. */
#define BOOT_HANDOVER_OPT_QUIET 1

/* Offsets, in unsigned longs: entry.S reads the block before there is C. */
#define HANDOVER_MAGIC (0 * REGBYTES)
#define HANDOVER_VERSION (1 * REGBYTES)
#define HANDOVER_BOOT_HART (5 * REGBYTES)

#ifndef __ASSEMBLY__

struct boot_handover {
	unsigned long magic;
	unsigned long version;
	unsigned long next_addr;
	unsigned long next_mode;
	unsigned long options;
	unsigned long boot_hart; /* version 2; all ones: any */
};

#endif

#endif
