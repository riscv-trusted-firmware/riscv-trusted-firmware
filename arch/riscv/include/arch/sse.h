/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_SSE_H
#define ARCH_SSE_H

/*
 * Supervisor software events (SBI SSE): events the monitor injects into
 * S-mode ahead of every trap and interrupt S-mode could mask. Semantics
 * and error codes are those of the SBI specification.
 *
 * Supported events: the software injected local and global events. The
 * other standard events have no source in this firmware yet (RAS, double
 * trap, PMU overflow) and are SBI_ERR_NOT_SUPPORTED.
 */

#include <arch/trap.h>
#include <util.h>

#define SSE_EVENT_LOCAL_SOFTWARE UL(0xffff0000)
#define SSE_EVENT_GLOBAL_SOFTWARE UL(0xffff8000)

#define SSE_ATTR_STATUS 0
#define SSE_ATTR_PRIORITY 1
#define SSE_ATTR_CONFIG 2
#define SSE_ATTR_PREFERRED_HART 3
#define SSE_ATTR_ENTRY_PC 4
#define SSE_ATTR_ENTRY_ARG 5
#define SSE_ATTR_INTERRUPTED_SEPC 6
#define SSE_ATTR_INTERRUPTED_FLAGS 7
#define SSE_ATTR_INTERRUPTED_A6 8
#define SSE_ATTR_INTERRUPTED_A7 9
#define SSE_ATTR_COUNT 10

#define SSE_STATE_UNUSED 0
#define SSE_STATE_REGISTERED 1
#define SSE_STATE_ENABLED 2
#define SSE_STATE_RUNNING 3
#define SSE_STATUS_STATE_MASK UL(3)
#define SSE_STATUS_PENDING BIT(2)
#define SSE_STATUS_INJECTABLE BIT(3)

#define SSE_CONFIG_ONESHOT BIT(0)

#ifdef CONFIG_SBI_SSE

/*
 * Every hart, before it enters the next stage: events masked, local ones
 * unused.
 */
void sse_hart_init(void);

/*
 * End of every trap taken from S/U-mode: if an event is due on this hart,
 * rewrite 'regs' so that the return lands in its handler.
 */
void sse_process(struct trap_regs *regs);
/* Is an event waiting for this hart to return to S-mode? */
bool sse_pending(void);

long sse_read_attrs(unsigned long event_id, unsigned long base,
		    unsigned long count, unsigned long addr);
long sse_write_attrs(unsigned long event_id, unsigned long base,
		     unsigned long count, unsigned long addr);
long sse_register(unsigned long event_id, unsigned long entry_pc,
		  unsigned long entry_arg);
long sse_unregister(unsigned long event_id);
long sse_enable(unsigned long event_id);
long sse_disable(unsigned long event_id);
/* true: 'regs' now holds the interrupted context, leave it alone. */
bool sse_complete(struct trap_regs *regs);
long sse_inject(unsigned long event_id, unsigned long hartid);
long sse_hart_unmask(void);
long sse_hart_mask(void);

#else

static inline void sse_hart_init(void)
{
}

static inline void sse_process(struct trap_regs *regs)
{
}

static inline bool sse_pending(void)
{
	return false;
}

#endif

#endif
