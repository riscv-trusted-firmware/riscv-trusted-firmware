/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#define IMAGE_BASE	CONFIG_SBITEST_LOAD_ADDR
#define IMAGE_SIZE	CONFIG_SBITEST_SIZE
/* Its stacks are an array of its own (start.S). */
#define IMAGE_STACKS	0
#include <arch/image.lds.h>
