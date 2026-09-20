/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef MEMREGION_H
#define MEMREGION_H

#include <types_ext.h>

/*
 * What the monitor tells its memory protection about the address map.
 * Drivers register the registers they drive at probe time; the arch code
 * turns the list into PMP entries on every hart.
 *
 *   MEMREGION_MMODE_RW   M-mode only: the monitor's devices (CLINT, the
 *                        machine-level interrupt controller files, ...)
 *   MEMREGION_MMODE_RX   M-mode only, code and constants
 *   MEMREGION_SHARED_RW  a device both M-mode and S-mode drive (console,
 *                        reset): matters once M-mode itself is confined
 *                        (Smepmp), where it may only touch what a rule
 *                        gives it
 *
 * Boot hart only, before the next stage is entered.
 */

enum memregion_kind {
	MEMREGION_MMODE_RX,
	MEMREGION_MMODE_RW,
	MEMREGION_SHARED_RW,
};

#ifdef IMAGE_MONITOR
void memregion_add(paddr_t base, paddr_size_t size, enum memregion_kind kind);
#else
/* Images that never leave M-mode protect nothing. */
static inline void memregion_add(paddr_t base, paddr_size_t size,
				 enum memregion_kind kind)
{
}
#endif

#endif
