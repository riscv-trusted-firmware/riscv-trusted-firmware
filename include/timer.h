/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef TIMER_H
#define TIMER_H

/*
 * Platform timer: one backend (a driver calls timer_register()), giving
 * the monitor a time base and S-mode its timer event. With Sstc the event
 * is programmed straight into stimecmp and the backend only keeps time.
 */

#include <stdbool.h>
#include <stdint.h>

struct timer_ops {
	const char *name;
	uint64_t (*now)(void);
	/* Per-hart M-mode compare register of the calling hart. */
	void (*set_event)(uint64_t when);
	void (*stop_event)(void);
};

void timer_register(const struct timer_ops *ops);
bool timer_available(void);
const char *timer_name(void);
uint64_t timer_now(void);

/* Calling hart: quiesce the compare register. */
void timer_hart_init(void);
/* Calling hart: (re)program the S-mode timer event, SBI set_timer semantics. */
void timer_smode_set(uint64_t when);
/* M-mode timer interrupt: turn it into a pending S-mode timer interrupt. */
void timer_process(void);

#endif
