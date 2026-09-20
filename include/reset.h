/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef RESET_H
#define RESET_H

/*
 * System reset. Drivers register backends; for a given reset type the
 * supporting backend with the highest rating is used.
 */

#include <compiler.h>
#include <stdbool.h>

enum reset_type {
	RESET_SHUTDOWN,
	RESET_COLD_REBOOT,
	RESET_WARM_REBOOT,
};

struct reset_ops {
	const char *name;
	unsigned int rating;
	bool (*supported)(enum reset_type type);
	/*
	 * Returns when the reset did not happen, or not yet: a backend may
	 * only have asked someone else for it.
	 */
	void (*reset)(enum reset_type type);
};

void reset_register(const struct reset_ops *ops);
/* The registered backends, best first. */
const char *reset_name(void);
bool reset_supported(enum reset_type type);

/* Reset, halt the other harts, and halt. */
void __noreturn system_reset(enum reset_type type);

#endif
