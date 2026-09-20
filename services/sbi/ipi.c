// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI IPI extension (EID "sPI").
 */

#include <arch/hart.h>
#include <domain.h>
#include <ipi.h>
#include <util.h>

#include "sbi_internal.h"

static long sbi_ipi_probe(unsigned long eid)
{
	return ipi_available();
}

#ifdef CONFIG_DOMAINS
/*
 * A hart of the domain that is away in another one is no target for an
 * interrupt now, and losing it is not an option: it finds it when it is back.
 */
static void ipi_parked_harts(unsigned long hmask, unsigned long hbase)
{
	const struct domain *dom = this_domain();
	unsigned int i = 0;

	if (!domain_has_parked(dom))
		return;
	for_each_hart_in_mask(i, &dom->parked) {
		unsigned long id = hart_id_of(i);

		if (hbase == ~UL(0) ||
		    (id >= hbase && id - hbase < BITS_PER_LONG &&
		     ((hmask >> (id - hbase)) & 1)))
			domain_ipi_parked(i);
	}
}
#else
static void ipi_parked_harts(unsigned long hmask, unsigned long hbase)
{
}
#endif

static struct service_ret sbi_ipi_ecall(unsigned long eid, unsigned long fid,
					struct trap_regs *regs)
{
	struct hartmask targets = {};
	long rc = 0;

	if (fid != SBI_IPI_SEND_IPI || !ipi_available())
		return sbi_err(SBI_ERR_NOT_SUPPORTED);

	rc = sbi_hartmask(regs->a0, regs->a1, &targets);
	if (rc)
		return sbi_err(rc);
	ipi_send_mask(&targets, IPI_EVENT_SMODE);
	ipi_parked_harts(regs->a0, regs->a1);
	return sbi_ok(0);
}

SERVICE_DEFINE(sbi_ipi) = {
	.name = "sbi-ipi",
	.eid_min = SBI_EXT_IPI,
	.eid_max = SBI_EXT_IPI,
	.probe = sbi_ipi_probe,
	.ecall = sbi_ipi_ecall,
};
