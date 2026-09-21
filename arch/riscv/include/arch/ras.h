/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_RAS_H
#define ARCH_RAS_H

/*
 * Where the RAS events of SBI SSE come from.
 *
 * A hart's own errors are the local RAS interrupts the AIA has numbers
 * for: 35, low priority, and 43, high priority, which is how the error
 * records of a hart (RERI) ask for attention. The monitor keeps them to
 * M-mode and takes one as the local RAS event of that priority. They are
 * level interrupts and stay up until S-mode has dealt with the error
 * record, so one is only enabled while its event is, and is kept off from
 * the moment it is taken until the event's handler has completed.
 *
 * What else can report an error comes with the platform: an error
 * collector behind a wired interrupt or an MSI, a PuC. Such a source says
 * which kinds of event it has with ras_source_register(), from plat_init()
 * or a driver's probe, and ras_report() when there is one: a local event
 * is for the calling hart and the domain it runs, a global one for every
 * domain that has registered it.
 *
 * An event nothing can raise is SBI_ERR_NOT_SUPPORTED, as SSE has it. The
 * records themselves are S-mode's to read; where there is a PuC, the
 * RAS_AGENT group of RPMI, which the monitor passes on through MPXY,
 * describes them.
 */

#include <stdbool.h>
#include <stdint.h>
#include <util.h>

enum ras_kind {
	RAS_LOCAL_HIGH,
	RAS_GLOBAL_HIGH,
	RAS_LOCAL_LOW,
	RAS_GLOBAL_LOW,
	RAS_KINDS,
};

#define SSE_EVENT_LOCAL_HIGH_RAS UL(0x00000000)
#define SSE_EVENT_GLOBAL_HIGH_RAS UL(0x00008000)
#define SSE_EVENT_LOCAL_LOW_RAS UL(0x00100000)
#define SSE_EVENT_GLOBAL_LOW_RAS UL(0x00108000)

#ifdef CONFIG_SBI_SSE

void ras_source_register(enum ras_kind kind);
void ras_report(enum ras_kind kind);

/*
 * For SSE: is 'event_id' a RAS event, and can anything raise it on this hart?
 */
bool ras_is_event(uint32_t event_id);
bool ras_event_available(uint32_t event_id);
/* The calling hart's local event is (not) ENABLED or RUNNING. */
void ras_event_arm(uint32_t event_id, bool armed);
/* An M-mode interrupt: true when it was a RAS one, which is an event now. */
bool ras_irq(unsigned long irq);
/*
 * Is the calling hart's RAS interrupt of that event there (hart feature probe)?
 */
bool ras_irqs_probe(void);

#else

static inline void ras_source_register(enum ras_kind kind)
{
}

static inline void ras_report(enum ras_kind kind)
{
}

static inline bool ras_irq(unsigned long irq)
{
	return false;
}

static inline bool ras_irqs_probe(void)
{
	return false;
}

#endif

#endif
