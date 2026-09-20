/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * Interface every platform/<vendor>/<board>/ implements.
 */

/*
 * Boot hart, before anything else: bring up the console. 'fdt' is what the
 * previous stage passed, unchecked (it may be anything, or NULL).
 */
void plat_early_init(const void *fdt);

/*
 * Boot hart, monitor only, before the drivers look at the device tree: the
 * platform may complete it. The tree is writable and has room to grow.
 * 0 or a libfdt error.
 */
int plat_fdt_prepare(void *fdt);

/* Boot hart, after drivers and services are initialised. */
void plat_init(void);

#endif
