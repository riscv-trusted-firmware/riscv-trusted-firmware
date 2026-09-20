// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI supervisor software events extension (EID "SSE").
 */

#include <arch/sse.h>

#include "sbi_internal.h"

#define SBI_SSE_READ_ATTRS 0
#define SBI_SSE_WRITE_ATTRS 1
#define SBI_SSE_REGISTER 2
#define SBI_SSE_UNREGISTER 3
#define SBI_SSE_ENABLE 4
#define SBI_SSE_DISABLE 5
#define SBI_SSE_COMPLETE 6
#define SBI_SSE_INJECT 7
#define SBI_SSE_HART_UNMASK 8
#define SBI_SSE_HART_MASK 9

static struct service_ret sbi_sse_ecall(unsigned long eid, unsigned long fid,
					struct trap_regs *regs)
{
	switch (fid) {
	case SBI_SSE_READ_ATTRS:
	case SBI_SSE_WRITE_ATTRS:
		/* (event_id, base_attr_id, attr_count, phys_lo, phys_hi) */
		if (regs->a4)
			return sbi_err(SBI_ERR_INVALID_ADDRESS);
		return sbi_err(fid == SBI_SSE_READ_ATTRS ?
			       sse_read_attrs(regs->a0, regs->a1,
					      regs->a2, regs->a3) :
			       sse_write_attrs(regs->a0, regs->a1,
					       regs->a2, regs->a3));
	case SBI_SSE_REGISTER:
		return sbi_err(sse_register(regs->a0, regs->a1, regs->a2));
	case SBI_SSE_UNREGISTER:
		return sbi_err(sse_unregister(regs->a0));
	case SBI_SSE_ENABLE:
		return sbi_err(sse_enable(regs->a0));
	case SBI_SSE_DISABLE:
		return sbi_err(sse_disable(regs->a0));
	case SBI_SSE_COMPLETE:
		/*
		 * On completion the register file is the interrupted
		 * context's, a0 and a1 included: nothing is returned.
		 */
		if (sse_complete(regs))
			return (struct service_ret){ .keep_regs = true };
		return sbi_ok(0);
	case SBI_SSE_INJECT:
		return sbi_err(sse_inject(regs->a0, regs->a1));
	case SBI_SSE_HART_UNMASK:
		return sbi_err(sse_hart_unmask());
	case SBI_SSE_HART_MASK:
		return sbi_err(sse_hart_mask());
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_sse) = {
	.name = "sbi-sse",
	.eid_min = SBI_EXT_SSE,
	.eid_max = SBI_EXT_SSE,
	.ecall = sbi_sse_ecall,
};
