/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBI_INTERNAL_H
#define SBI_INTERNAL_H

#include <hartmask.h>
#include <sbi/sbi.h>
#include <service.h>

static inline struct service_ret sbi_ret(long error, long value)
{
	return (struct service_ret){ error, value };
}

static inline struct service_ret sbi_ok(long value)
{
	return sbi_ret(SBI_SUCCESS, value);
}

static inline struct service_ret sbi_err(long error)
{
	return sbi_ret(error, 0);
}

/*
 * Turn an SBI (hart_mask, hart_mask_base) pair into the set of harts to
 * act on. hart_mask_base == -1 selects every hart. Harts that are not
 * running the next stage are dropped silently; a hart id that does not
 * exist is SBI_ERR_INVALID_PARAM.
 */
long sbi_hartmask(unsigned long hmask, unsigned long hbase,
		  struct hartmask *out);

#endif
