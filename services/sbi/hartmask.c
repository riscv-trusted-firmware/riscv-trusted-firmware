// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <util.h>

#include "sbi_internal.h"

long sbi_hartmask(unsigned long hmask, unsigned long hbase,
		  struct hartmask *out)
{
	struct hartmask running = {};

	hsm_interruptible_mask(&running);
	if (hbase == ~UL(0)) {
		*out = running;
		return SBI_SUCCESS;
	}

	hartmask_clear_all(out);
	for (unsigned long i = 0; i < BITS_PER_LONG; i++) {
		unsigned long hartid = hbase + i;

		if (!(hmask & BIT(i)))
			continue;
		if (hartid < hbase || !hart_valid(hartid))
			return SBI_ERR_INVALID_PARAM;
		/* S-mode names harts by id, the monitor keeps them by index. */
		if (hartmask_test(&running, (unsigned int)hart_index(hartid)))
			hartmask_set(out, (unsigned int)hart_index(hartid));
	}
	return SBI_SUCCESS;
}
