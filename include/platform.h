/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * Interface every platform/<vendor>/<board>/ implements.
 */

/* Boot hart, before anything else: bring up the console. */
void plat_early_init(void);

/* Boot hart, after drivers and services are initialised. */
void plat_init(void);

#endif
