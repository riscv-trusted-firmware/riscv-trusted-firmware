/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef CPPC_H
#define CPPC_H

/*
 * Collaborative processor performance control (ACPI CPPC registers, SBI
 * CPPC extension): one platform backend, acting on the calling hart's
 * registers. Return values are SBI error codes.
 */

#include <stdbool.h>
#include <stdint.h>

struct cppc_ops {
	const char *name;
	/* *width: register width in bits, 0 if it is not implemented. */
	long (*probe)(uint32_t reg, uint32_t *width);
	long (*read)(uint32_t reg, uint64_t *val);
	long (*write)(uint32_t reg, uint64_t val);
};

void cppc_register(const struct cppc_ops *ops);
bool cppc_available(void);
const char *cppc_name(void);

long cppc_probe(uint32_t reg, uint32_t *width);
long cppc_read(uint32_t reg, uint64_t *val);
long cppc_write(uint32_t reg, uint64_t val);

#endif
