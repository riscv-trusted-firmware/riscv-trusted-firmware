// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <sbi/sbi.h>
#include <stddef.h>
#include <suspend.h>

static const struct suspend_ops *suspend;

void suspend_register(const struct suspend_ops *ops)
{
	suspend = ops;
}

const char *suspend_name(void)
{
	return suspend ? suspend->name : "none";
}

bool suspend_supported(uint32_t sleep_type)
{
	return suspend && suspend->supported(sleep_type);
}

long suspend_prepare(uint32_t sleep_type, unsigned long resume_addr)
{
	if (!suspend_supported(sleep_type))
		return SBI_ERR_NOT_SUPPORTED;
	return suspend->prepare(sleep_type, resume_addr);
}
