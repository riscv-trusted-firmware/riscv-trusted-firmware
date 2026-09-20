// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Domain control, a firmware specific SBI extension.
 *
 * The SBI specification knows nothing of domains; what S-mode needs of
 * them goes where it leaves room for such things, the firmware specific
 * extension space (0x0A000000 + the SBI implementation id). A domain goes
 * by its index: 0 is the root domain, the ones of the device tree follow
 * in the order of their nodes.
 *
 * Entering a domain is open to every domain the hart is a possible hart
 * of: that is what the device tree says with "possible-harts". Starting
 * and stopping another domain takes the right to reset the system, which
 * is no less.
 */

#include <domain.h>

#include "sbi_internal.h"

static struct service_ret switched(long rc)
{
	/* The register file is the other context's now, results and all. */
	return rc ? sbi_err(rc) : (struct service_ret){ .keep_regs = true };
}

static struct service_ret sbi_domain_ecall(unsigned long eid, unsigned long fid,
					   struct trap_regs *regs)
{
	struct domain *dom = domain_by_index((unsigned int)regs->a0);

	switch (fid) {
	case SBI_FW_DOMAIN_COUNT:
		return sbi_ok((long)domain_count());
	case SBI_FW_DOMAIN_SELF:
		return sbi_ok((long)this_domain()->index);
	case SBI_FW_DOMAIN_EXIT:
		return switched(domain_exit(regs, regs->a0));
	default:
		break;
	}

	if (fid > SBI_FW_DOMAIN_STATE)
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	if (regs->a0 >= domain_count())
		return sbi_err(SBI_ERR_INVALID_PARAM);

	switch (fid) {
	case SBI_FW_DOMAIN_ENTER:
		return switched(domain_enter(regs, dom, regs->a1));
	case SBI_FW_DOMAIN_STATE:
		return sbi_ok(domain_running(dom));
	default:
		break;
	}

	if (dom != this_domain() && !domain_reset_allowed(this_domain()))
		return sbi_err(SBI_ERR_DENIED);
	if (fid == SBI_FW_DOMAIN_START)
		return sbi_err(domain_start(dom));
	/* Stopping its own domain is the end of the caller, and of the call. */
	return sbi_err(domain_stop(regs, dom));
}

SERVICE_DEFINE(sbi_fw_domain) = {
	.name = "sbi-fw-domain",
	.eid_min = SBI_EXT_FW_DOMAIN,
	.eid_max = SBI_EXT_FW_DOMAIN,
	.ecall = sbi_domain_ecall,
};
