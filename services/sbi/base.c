// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI base extension (EID 0x10), mandatory for every SBI implementation.
 */

#include <arch/csr.h>
#include <sbi/sbi.h>
#include <service.h>

static struct service_ret ok(long value)
{
	return (struct service_ret){ .error = SBI_SUCCESS, .value = value };
}

static struct service_ret sbi_base_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	switch (fid) {
	case SBI_BASE_GET_SPEC_VERSION:
		return ok((SBI_SPEC_VERSION_MAJOR << 24) |
			  SBI_SPEC_VERSION_MINOR);
	case SBI_BASE_GET_IMPL_ID:
		return ok(CONFIG_SBI_IMPL_ID);
	case SBI_BASE_GET_IMPL_VERSION:
		return ok(CONFIG_SBI_IMPL_VERSION);
	case SBI_BASE_PROBE_EXTENSION:
		return ok(service_probe(regs->a0));
	case SBI_BASE_GET_MVENDORID:
		return ok((long)csr_read(mvendorid));
	case SBI_BASE_GET_MARCHID:
		return ok((long)csr_read(marchid));
	case SBI_BASE_GET_MIMPID:
		return ok((long)csr_read(mimpid));
	default:
		return (struct service_ret){ .error = SBI_ERR_NOT_SUPPORTED };
	}
}

SERVICE_DEFINE(sbi_base) = {
	.name = "sbi-base",
	.eid_min = SBI_EXT_BASE,
	.eid_max = SBI_EXT_BASE,
	.ecall = sbi_base_ecall,
};
