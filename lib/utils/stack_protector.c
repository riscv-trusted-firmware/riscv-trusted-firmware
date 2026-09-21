// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* What -fstack-protector wants of its runtime: the canary, and the way out. */

#include <log.h>
#include <stdint.h>
#include <util.h>

void __stack_chk_fail(void);

/*
 * A zero byte and a newline in it: string functions stop short of rewriting it.
 */
#if __RISCV_XLEN__ == 64
const uintptr_t __stack_chk_guard = UL(0x5ac3e1b7000a9d42);
#else
const uintptr_t __stack_chk_guard = UL(0x5a000ad4);
#endif

void __stack_chk_fail(void)
{
	panic("stack overflow: a canary is gone\n");
}
