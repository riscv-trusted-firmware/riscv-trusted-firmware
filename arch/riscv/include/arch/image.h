/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_IMAGE_H
#define ARCH_IMAGE_H

/*
 * The image as the linker script laid it out (<arch/image.lds.h>): symbols
 * with an address and nothing behind it.
 */

extern char __image_start[];
/* IMAGE_TEXT_RODATA_END: code and constants before, data after */
extern char __text_rodata_end[];
/* IMAGE_HEAP: where the boot heap begins */
extern char __heap_start[];

#endif
