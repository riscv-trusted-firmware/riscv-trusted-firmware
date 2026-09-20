// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The vendor specific SBI extension space: 0x09000000 + the low 24 bits of
 * mvendorid is the hart vendor's to define, and a platform that has such
 * calls registers them (<sbi/vendor.h>). Every other id of the space, and
 * that one without a platform behind it, is not there.
 */

#include <arch/csr.h>
#include <sbi/vendor.h>

#include "sbi_internal.h"

static const struct sbi_vendor_ops *vendor;

void sbi_vendor_register(const struct sbi_vendor_ops *ops)
{
	vendor = ops;
}

static unsigned long vendor_eid(void)
{
	return SBI_EXT_VENDOR_START + (csr_read(mvendorid) & 0xffffff);
}

static long sbi_vendor_probe(unsigned long eid)
{
	if (!vendor || eid != vendor_eid())
		return 0;
	return vendor->probe ? vendor->probe() : 1;
}

static struct service_ret sbi_vendor_ecall(unsigned long eid, unsigned long fid,
					   struct trap_regs *regs)
{
	if (!sbi_vendor_probe(eid))
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	return vendor->ecall(fid, regs);
}

SERVICE_DEFINE(sbi_vendor) = {
	.name = "sbi-vendor",
	.eid_min = SBI_EXT_VENDOR_START,
	.eid_max = SBI_EXT_VENDOR_END,
	.probe = sbi_vendor_probe,
	.ecall = sbi_vendor_ecall,
};
