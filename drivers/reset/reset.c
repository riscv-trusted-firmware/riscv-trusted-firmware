// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <ipi.h>
#include <reset.h>
#include <timer.h>

#define MAX_BACKENDS 4
/* How long an asynchronous reset gets to take effect before we give up. */
#define RESET_GRACE_US 200000

static const struct reset_ops *backends[MAX_BACKENDS];
static unsigned int nr_backends;

void reset_register(const struct reset_ops *ops)
{
	unsigned int i = 0;

	if (nr_backends == MAX_BACKENDS)
		return;
	/* Sorted by rating, best first. */
	for (i = nr_backends++; i && backends[i - 1]->rating < ops->rating; i--)
		backends[i] = backends[i - 1];
	backends[i] = ops;
}

const char *reset_name(void)
{
	return nr_backends ? backends[0]->name : "none";
}

static const struct reset_ops *backend_for(enum reset_type type)
{
	for (unsigned int i = 0; i < nr_backends; i++)
		if (backends[i]->supported(type))
			return backends[i];
	return NULL;
}

bool reset_supported(enum reset_type type)
{
	return backend_for(type);
}

void __noreturn system_reset(enum reset_type type)
{
	const struct reset_ops *ops = backend_for(type);
	unsigned long self = this_hartid();
	uint64_t until = 0;

	if (ops) {
		ops->reset(type);
		/*
		 * Still here: someone else carries it out. Give them a moment.
		 */
		until = timer_now() + timer_usecs_to_ticks(RESET_GRACE_US);
		while (timer_available() && timer_now() < until)
			cpu_relax();
	}

	for (unsigned long h = 0; h < CONFIG_PLATFORM_HART_COUNT; h++)
		if (h != self && hart_valid(h))
			ipi_send(h, IPI_EVENT_HALT);
	hart_halt();
}
