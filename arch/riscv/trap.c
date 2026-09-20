// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * M-mode trap dispatch. Environment calls from lower privilege levels are
 * routed to the service layer; everything else is fatal for now.
 */

#include <arch/trap.h>
#include <log.h>
#include <service.h>

static void dump_regs(const struct trap_regs *r)
{
	pr_err("mcause=%lx mtval=%lx mepc=%lx mstatus=%lx\n", csr_read(mcause),
	       csr_read(mtval), r->mepc, r->mstatus);
	pr_err("ra=%lx sp=%lx gp=%lx tp=%lx\n", r->ra, r->sp, r->gp, r->tp);
	pr_err("a0=%lx a1=%lx a2=%lx a3=%lx\n", r->a0, r->a1, r->a2, r->a3);
	pr_err("a4=%lx a5=%lx a6=%lx a7=%lx\n", r->a4, r->a5, r->a6, r->a7);
}

void trap_handler(struct trap_regs *regs)
{
	unsigned long cause = csr_read(mcause);

	if (cause & CAUSE_IRQ_FLAG) {
		/* Interrupt controller / timer / IPI drivers plug in here. */
		panic("unhandled interrupt %lu\n", cause & ~CAUSE_IRQ_FLAG);
	}

	switch (cause) {
	case CAUSE_SUPERVISOR_ECALL:
	case CAUSE_USER_ECALL: {
		struct service_ret ret = service_ecall(regs);

		regs->a0 = (unsigned long)ret.error;
		regs->a1 = (unsigned long)ret.value;
		regs->mepc += 4;
		return;
	}
	default:
		dump_regs(regs);
		panic("unhandled exception %lu\n", cause);
	}
}
