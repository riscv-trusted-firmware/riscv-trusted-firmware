// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI v0.1 legacy extensions (EIDs 0x00-0x08). Deprecated, kept for old
 * supervisor software. Differences from the v0.2+ calling convention:
 * only a0 is returned, and hart masks are passed as the *virtual* address
 * of an unsigned long, read here the way the caller would (NULL = all).
 */

#include <domain.h>
#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/rfence.h>
#include <arch/unpriv.h>
#include <console.h>
#include <ipi.h>
#include <reset.h>
#include <timer.h>
#include <util.h>

#include "sbi_internal.h"

/* false: reading the mask faulted and the fault went back to the caller. */
static bool legacy_hartmask(struct trap_regs *regs, unsigned long va,
			    struct hartmask *out)
{
	struct trap_info fault = {};
	unsigned long hmask = 0;

	if (!va) {
		hsm_interruptible_mask(out);
		return true;
	}
	if (!unpriv_read(regs, va, sizeof(hmask), &hmask, &fault)) {
		/* Retry the ecall once S-mode has dealt with the fault. */
		regs->mepc -= 4;
		trap_redirect(regs, &fault);
		return false;
	}
	/*
	 * Bit n is hart id n. v0.1 has no error for unknown harts: ignore them.
	 */
	hsm_interruptible_mask(out);
	for (unsigned int i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++) {
		unsigned long hartid = hart_id_of(i);

		if (hartid >= BITS_PER_LONG || !(hmask & BIT(hartid)))
			hartmask_clear(out, i);
	}
	return true;
}

static struct service_ret sbi_legacy_ecall(unsigned long eid, unsigned long fid,
					   struct trap_regs *regs)
{
	struct rfence_req req = { .start = regs->a1, .size = regs->a2 };
	struct hartmask targets = {};
	uint64_t when = 0;
	int c = 0;

	switch (eid) {
	case SBI_EXT_LEGACY_SET_TIMER:
		when = regs->a0;
#if __RISCV_XLEN__ == 32
		when |= SHIFT_U64(regs->a1, 32);
#endif
		timer_smode_set(when);
		return sbi_ok(0);
	case SBI_EXT_LEGACY_PUTCHAR:
		console_write(&(char){ (char)regs->a0 }, 1);
		return sbi_ok(0);
	case SBI_EXT_LEGACY_GETCHAR:
		c = console_getc();
		return sbi_err(c); /* the character, or -1 */
	case SBI_EXT_LEGACY_CLEAR_IPI:
		csr_clear(mip, MIP_SSIP);
		return sbi_ok(0);
	case SBI_EXT_LEGACY_SEND_IPI:
		if (!legacy_hartmask(regs, regs->a0, &targets))
			break;
		ipi_send_mask(&targets, IPI_EVENT_SMODE);
		return sbi_ok(0);
	case SBI_EXT_LEGACY_RFENCE_I:
	case SBI_EXT_LEGACY_SFENCE_VMA:
	case SBI_EXT_LEGACY_SFENCE_VMA_ASID:
		if (!legacy_hartmask(regs, regs->a0, &targets))
			break;
		req.type = eid == SBI_EXT_LEGACY_RFENCE_I ?
				   RFENCE_FENCE_I :
			   eid == SBI_EXT_LEGACY_SFENCE_VMA ?
				   RFENCE_SFENCE_VMA :
				   RFENCE_SFENCE_VMA_ASID;
		req.asid = regs->a3;
		return sbi_err(rfence_request(&targets, &req));
	case SBI_EXT_LEGACY_SHUTDOWN:
		if (!domain_reset_allowed(this_domain()))
			return sbi_err(SBI_ERR_DENIED);
		system_reset(RESET_SHUTDOWN);
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}

	/* The call was turned into a fault: a0 must reach S-mode untouched. */
	return sbi_err((long)regs->a0);
}

SERVICE_DEFINE(sbi_legacy) = {
	.name = "sbi-legacy",
	.eid_min = SBI_EXT_LEGACY_SET_TIMER,
	.eid_max = SBI_EXT_LEGACY_SHUTDOWN,
	.flags = SERVICE_LEGACY_RET,
	.ecall = sbi_legacy_ecall,
};
