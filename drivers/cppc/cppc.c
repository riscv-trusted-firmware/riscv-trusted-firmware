// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <cppc.h>
#include <sbi/sbi.h>
#include <stddef.h>

static const struct cppc_ops *cppc;

void cppc_register(const struct cppc_ops *ops)
{
	cppc = ops;
}

bool cppc_available(void)
{
	return cppc;
}

const char *cppc_name(void)
{
	return cppc ? cppc->name : "none";
}

long cppc_probe(uint32_t reg, uint32_t *width)
{
	return cppc ? cppc->probe(reg, width) : SBI_ERR_NOT_SUPPORTED;
}

long cppc_read(uint32_t reg, uint64_t *val)
{
	return cppc ? cppc->read(reg, val) : SBI_ERR_NOT_SUPPORTED;
}

long cppc_write(uint32_t reg, uint64_t val)
{
	return cppc ? cppc->write(reg, val) : SBI_ERR_NOT_SUPPORTED;
}
