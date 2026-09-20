// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI system reset extension (EID "SRST").
 */

#include <log.h>
#include <reset.h>
#include <util.h>

#include "sbi_internal.h"

static long sbi_srst_probe(unsigned long eid)
{
	return reset_supported(RESET_SHUTDOWN) ||
	       reset_supported(RESET_COLD_REBOOT) ||
	       reset_supported(RESET_WARM_REBOOT);
}

static struct service_ret sbi_srst_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	unsigned long type = regs->a0, reason = regs->a1;
	enum reset_type rt = 0;

	if (fid != SBI_SRST_SYSTEM_RESET)
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	/*
	 * Both are 32-bit values; 0xF0000000 and up are vendor/platform ones.
	 */
	if (type > UL(0xffffffff) || reason > UL(0xffffffff))
		return sbi_err(SBI_ERR_INVALID_PARAM);
	if (reason > SBI_SRST_REASON_SYSTEM_FAILURE && reason < UL(0xe0000000))
		return sbi_err(SBI_ERR_INVALID_PARAM);

	switch (type) {
	case SBI_SRST_TYPE_SHUTDOWN:
		rt = RESET_SHUTDOWN;
		break;
	case SBI_SRST_TYPE_COLD_REBOOT:
		rt = RESET_COLD_REBOOT;
		break;
	case SBI_SRST_TYPE_WARM_REBOOT:
		rt = RESET_WARM_REBOOT;
		break;
	default:
		return sbi_err(type >= UL(0xf0000000) ? SBI_ERR_NOT_SUPPORTED :
							SBI_ERR_INVALID_PARAM);
	}
	if (!reset_supported(rt))
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	pr_dbg("sbi-srst: type %lu reason %lu\n", type, reason);
	system_reset(rt);
}

SERVICE_DEFINE(sbi_srst) = {
	.name = "sbi-srst",
	.eid_min = SBI_EXT_SRST,
	.eid_max = SBI_EXT_SRST,
	.probe = sbi_srst_probe,
	.ecall = sbi_srst_ecall,
};
