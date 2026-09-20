// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI debug triggers extension (EID "DBTR").
 */

#include <arch/dbtr.h>
#include <arch/hart.h>

#include "sbi_internal.h"

#define SBI_DBTR_NUM_TRIGGERS 0
#define SBI_DBTR_SET_SHMEM 1
#define SBI_DBTR_READ 2
#define SBI_DBTR_INSTALL 3
#define SBI_DBTR_UPDATE 4
#define SBI_DBTR_UNINSTALL 5
#define SBI_DBTR_ENABLE 6
#define SBI_DBTR_DISABLE 7

static struct service_ret sbi_dbtr_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	unsigned long failed = 0;
	long rc = 0;

	switch (fid) {
	case SBI_DBTR_NUM_TRIGGERS:
		return sbi_ok((long)dbtr_num_triggers(regs->a0));
	case SBI_DBTR_SET_SHMEM:
		return sbi_err(dbtr_set_shmem(regs->a0, regs->a1, regs->a2));
	case SBI_DBTR_READ:
		return sbi_err(dbtr_read(regs->a0, regs->a1));
	case SBI_DBTR_INSTALL:
		rc = dbtr_install(regs->a0, &failed);
		return sbi_ret(rc, (long)failed);
	case SBI_DBTR_UPDATE:
		rc = dbtr_update(regs->a0, &failed);
		return sbi_ret(rc, (long)failed);
	case SBI_DBTR_UNINSTALL:
		return sbi_err(dbtr_uninstall(regs->a0, regs->a1));
	case SBI_DBTR_ENABLE:
		return sbi_err(dbtr_enable(regs->a0, regs->a1));
	case SBI_DBTR_DISABLE:
		return sbi_err(dbtr_disable(regs->a0, regs->a1));
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

/* Without Sdtrig there is nothing to offer. */
static long sbi_dbtr_probe(unsigned long eid)
{
	return hart_has(HART_FEAT_SDTRIG);
}

SERVICE_DEFINE(sbi_dbtr) = {
	.name = "sbi-dbtr",
	.eid_min = SBI_EXT_DBTR,
	.eid_max = SBI_EXT_DBTR,
	.probe = sbi_dbtr_probe,
	.ecall = sbi_dbtr_ecall,
};
