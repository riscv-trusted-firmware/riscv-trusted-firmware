/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef IPI_H
#define IPI_H

/*
 * Inter-processor interrupts. A backend (a driver calls ipi_register())
 * raises and clears the M-mode software interrupt of a hart; the core
 * multiplexes events over it through a per-hart pending word.
 */

#include <hartmask.h>

enum ipi_event {
	IPI_EVENT_SMODE, /* raise the S-mode software interrupt */
	IPI_EVENT_RFENCE, /* serve the current remote fence request */
	IPI_EVENT_HALT, /* stop executing, for good */
	/* pass through the trap exit: an event is due */
	IPI_EVENT_SSE,
	IPI_EVENT_COUNT,
};

struct ipi_ops {
	const char *name;
	/* With several backends around, the highest rating wins. */
	unsigned int rating;
	/* The mip / mie bit the IPI arrives on: MIP_MSIP, or MIP_MEIP. */
	unsigned long irq;
	/* Calling hart: get ready to receive. May be NULL. */
	void (*hart_init)(void);
	/* Harts go by index here, as everywhere inside the monitor. */
	void (*send)(unsigned int index);
	/* Calling hart only: acknowledge what is pending. */
	void (*clear)(unsigned int index);
};

void ipi_register(const struct ipi_ops *ops);
/* The interrupt to enable and to look for in mip; 0 without a backend. */
unsigned long ipi_irq(void);
bool ipi_available(void);
const char *ipi_name(void);

/* Calling hart: drop stale state, enable the M-mode software interrupt. */
void ipi_hart_init(void);

void ipi_send(unsigned int index, enum ipi_event event);
void ipi_send_mask(const struct hartmask *mask, enum ipi_event event);
/* Wake a hart out of WFI without an event. */
void ipi_kick(unsigned int index);

/*
 * Handle everything pending for the calling hart. Called on the M-mode
 * software interrupt, and from M-mode loops that wait on another hart.
 */
void ipi_process(void);

#endif
