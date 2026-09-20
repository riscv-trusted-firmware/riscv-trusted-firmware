// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI message proxy extension (EID "MPXY").
 */

#include <mpxy.h>

#include "sbi_internal.h"

#define SBI_MPXY_GET_SHMEM_SIZE 0
#define SBI_MPXY_SET_SHMEM 1
#define SBI_MPXY_GET_CHANNEL_IDS 2
#define SBI_MPXY_READ_ATTRS 3
#define SBI_MPXY_WRITE_ATTRS 4
#define SBI_MPXY_SEND_MSG_WITH_RESP 5
#define SBI_MPXY_SEND_MSG_WITHOUT_RESP 6
#define SBI_MPXY_GET_NOTIFICATIONS 7

static struct service_ret sbi_mpxy_shmem_call(unsigned long fid,
					      struct trap_regs *regs)
{
	unsigned long out = 0;
	long rc = 0;

	switch (fid) {
	case SBI_MPXY_GET_CHANNEL_IDS:
		return sbi_err(mpxy_get_channel_ids(regs->a0));
	case SBI_MPXY_READ_ATTRS:
		return sbi_err(mpxy_read_attributes(regs->a0, regs->a1,
						    regs->a2));
	case SBI_MPXY_WRITE_ATTRS:
		return sbi_err(mpxy_write_attributes(regs->a0, regs->a1,
						     regs->a2));
	case SBI_MPXY_SEND_MSG_WITH_RESP:
		rc = mpxy_send_message(regs->a0, regs->a1, regs->a2, &out);
		return sbi_ret(rc, (long)out);
	case SBI_MPXY_SEND_MSG_WITHOUT_RESP:
		return sbi_err(mpxy_send_message(regs->a0, regs->a1, regs->a2,
						 NULL));
	case SBI_MPXY_GET_NOTIFICATIONS:
		rc = mpxy_get_notifications(regs->a0, &out);
		return sbi_ret(rc, (long)out);
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

static struct service_ret sbi_mpxy_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	struct service_ret ret = {};

	switch (fid) {
	case SBI_MPXY_GET_SHMEM_SIZE:
		return sbi_ok(MPXY_SHMEM_SIZE);
	case SBI_MPXY_SET_SHMEM:
		return sbi_err(mpxy_set_shmem(regs->a0, regs->a1, regs->a2));
	default:
		/* The rest works on the shared memory. */
		mpxy_shmem_access(true);
		ret = sbi_mpxy_shmem_call(fid, regs);
		mpxy_shmem_access(false);
		return ret;
	}
}

/* Without a channel there is nothing to proxy. */
static long sbi_mpxy_probe(unsigned long eid)
{
	return mpxy_channel_count() != 0;
}

SERVICE_DEFINE(sbi_mpxy) = {
	.name = "sbi-mpxy",
	.eid_min = SBI_EXT_MPXY,
	.eid_max = SBI_EXT_MPXY,
	.probe = sbi_mpxy_probe,
	.ecall = sbi_mpxy_ecall,
};
