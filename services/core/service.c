// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <log.h>
#include <sbi/sbi.h>
#include <service.h>

void services_init(void)
{
	const struct service *s = NULL;

	for_each_service(s) {
		int rc = s->init ? s->init() : 0;

		if (rc)
			panic("service %s: init failed (%d)\n", s->name, rc);
		pr_dbg("service %s: eid %lx-%lx\n", s->name, s->eid_min,
		       s->eid_max);
	}
}

const struct service *service_lookup(unsigned long eid)
{
	const struct service *s = NULL;

	for_each_service(s)
		if (eid >= s->eid_min && eid <= s->eid_max)
			return s;
	return NULL;
}

long service_probe(unsigned long eid)
{
	const struct service *s = service_lookup(eid);

	if (!s)
		return 0;
	return s->probe ? s->probe(eid) : 1;
}

void service_ecall(struct trap_regs *regs)
{
	const struct service *s = service_lookup(regs->a7);
	struct service_ret ret = { SBI_ERR_NOT_SUPPORTED, 0 };

	if (s && s->ecall)
		ret = s->ecall(regs->a7, regs->a6, regs);

	regs->a0 = (unsigned long)ret.error;
	if (!s || !(s->flags & SERVICE_LEGACY_RET))
		regs->a1 = (unsigned long)ret.value;
}
