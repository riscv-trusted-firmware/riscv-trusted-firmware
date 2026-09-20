// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* The loader never leaves M-mode and takes no interrupts: any trap is fatal. */

#include <arch/trap.h>

void trap_handler(struct trap_regs *regs)
{
	trap_fatal(regs, "loader: unexpected trap");
}
