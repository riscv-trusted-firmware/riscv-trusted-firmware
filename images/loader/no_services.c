// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* The loader takes no ecalls: any trap that reaches the dispatcher is fatal. */

#include <log.h>
#include <service.h>

struct service_ret service_ecall(struct trap_regs *regs)
{
	panic("loader: unexpected ecall eid=%lx\n", regs->a7);
}
