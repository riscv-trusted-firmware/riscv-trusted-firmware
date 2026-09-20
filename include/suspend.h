/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SUSPEND_H
#define SUSPEND_H

/*
 * System suspend: an optional platform backend that is told when the
 * system goes to sleep (SBI SUSP sleep types). The hart then waits for a
 * wake-up interrupt and resumes at the address S-mode gave, as it does
 * without a backend.
 */

#include <stdbool.h>
#include <stdint.h>

struct suspend_ops {
	const char *name;
	bool (*supported)(uint32_t sleep_type);
	/* 0 or an SBI error; the caller is the last hart running. */
	long (*prepare)(uint32_t sleep_type, unsigned long resume_addr);
};

void suspend_register(const struct suspend_ops *ops);
const char *suspend_name(void);
bool suspend_supported(uint32_t sleep_type);
long suspend_prepare(uint32_t sleep_type, unsigned long resume_addr);

#endif
