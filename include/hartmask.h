/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef HARTMASK_H
#define HARTMASK_H

/* Bitmap of hart ids, sized by CONFIG_PLATFORM_HART_COUNT. */

#include <atomic.h>
#include <bitstring.h>
#include <stdbool.h>

#define BITS_PER_LONG (8 * sizeof(unsigned long))

struct hartmask {
	bit_decl(bits, CONFIG_PLATFORM_HART_COUNT);
};

static inline void hartmask_clear_all(struct hartmask *m)
{
	for (unsigned int i = 0; i < bitstr_size(CONFIG_PLATFORM_HART_COUNT);
	     i++)
		m->bits[i] = 0;
}

static inline void hartmask_set(struct hartmask *m, unsigned long hartid)
{
	bit_set(m->bits, hartid);
}

static inline void hartmask_clear(struct hartmask *m, unsigned long hartid)
{
	bit_clear(m->bits, hartid);
}

static inline bool hartmask_test(const struct hartmask *m, unsigned long hartid)
{
	return bit_test(m->bits, hartid);
}

/* Atomic variants, for a mask shared between harts. */
static inline void hartmask_clear_atomic(struct hartmask *m,
					 unsigned long hartid)
{
	atomic_and_ulong(&m->bits[_bit_word(hartid)], ~_bit_mask(hartid));
}

static inline bool hartmask_empty_atomic(const struct hartmask *m)
{
	for (unsigned int i = 0; i < bitstr_size(CONFIG_PLATFORM_HART_COUNT);
	     i++)
		if (atomic_load_ulong(&m->bits[i]))
			return false;
	return true;
}

#define for_each_hart_in_mask(h, m) \
	bit_foreach(h, (m)->bits, CONFIG_PLATFORM_HART_COUNT)

#endif
