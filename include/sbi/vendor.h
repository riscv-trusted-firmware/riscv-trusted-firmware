/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBI_VENDOR_H
#define SBI_VENDOR_H

/*
 * Vendor specific SBI calls (EID 0x09000000 + mvendorid[23:0]): what they
 * are is between the hart vendor and its software, so they come with the
 * platform, which registers them from plat_init() or a driver's probe.
 */

#include <service.h>

struct sbi_vendor_ops {
	/* What sbi_probe_extension() reports; NULL: 1. */
	long (*probe)(void);
	/*
	 * a6 = fid, arguments in a0..a5 of 'regs'; the SBI (error, value) back.
	 */
	struct service_ret (*ecall)(unsigned long fid, struct trap_regs *regs);
};

#ifdef CONFIG_SBI_VENDOR
void sbi_vendor_register(const struct sbi_vendor_ops *ops);
#else
static inline void sbi_vendor_register(const struct sbi_vendor_ops *ops)
{
}
#endif

#endif
