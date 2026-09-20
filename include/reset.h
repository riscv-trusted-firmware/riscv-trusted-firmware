/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef RESET_H
#define RESET_H

/* System reset: one backend, registered by a driver. */

#include <compiler.h>
#include <stdbool.h>

enum reset_type {
	RESET_SHUTDOWN,
	RESET_COLD_REBOOT,
	RESET_WARM_REBOOT,
};

struct reset_ops {
	const char *name;
	bool (*supported)(enum reset_type type);
	/* Returns only when the reset did not happen. */
	void (*reset)(enum reset_type type);
};

void reset_register(const struct reset_ops *ops);
const char *reset_name(void);
bool reset_supported(enum reset_type type);

/* Halt the other harts, then reset (or halt as well, without a backend). */
void __noreturn system_reset(enum reset_type type);

#endif
