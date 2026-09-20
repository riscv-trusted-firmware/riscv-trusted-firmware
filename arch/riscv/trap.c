// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Trap reporting shared by every image. The trap policy itself
 * (trap_handler) belongs to the image: arch/riscv/runtime/ for the monitor.
 */

#include <arch/trap.h>
#include <log.h>

void __noreturn trap_fatal(const struct trap_regs *r, const char *what)
{
	pr_err("mcause=%lx mtval=%lx mepc=%lx mstatus=%lx\n", csr_read(mcause),
	       csr_read(mtval), r->mepc, r->mstatus);
	pr_err("ra=%lx sp=%lx gp=%lx tp=%lx\n", r->ra, r->sp, r->gp, r->tp);
	pr_err("a0=%lx a1=%lx a2=%lx a3=%lx\n", r->a0, r->a1, r->a2, r->a3);
	pr_err("a4=%lx a5=%lx a6=%lx a7=%lx\n", r->a4, r->a5, r->a6, r->a7);
	panic("%s\n", what);
}
