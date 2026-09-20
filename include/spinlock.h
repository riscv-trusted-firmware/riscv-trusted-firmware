/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

/*
 * Test-and-set spinlock. M-mode runs with interrupts off, so there is no
 * irqsave variant: a lock is never taken from a context that interrupted
 * its holder.
 */

#include <arch/csr.h>
#include <atomic.h>
#include <util.h>

/* A lock is an unsigned long: SPINLOCK_UNLOCK, or taken. */
#define SPINLOCK_UNLOCK UL(0)

static inline bool spin_trylock(unsigned long *lock)
{
	return atomic_swap_ulong(lock, 1) == 0;
}

static inline void spin_lock(unsigned long *lock)
{
	while (!spin_trylock(lock))
		cpu_relax();
}

static inline void spin_unlock(unsigned long *lock)
{
	atomic_store_ulong(lock, SPINLOCK_UNLOCK);
}

#endif
