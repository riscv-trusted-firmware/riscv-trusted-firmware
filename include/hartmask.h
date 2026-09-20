/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef HARTMASK_H
#define HARTMASK_H

/*
 * Bitmap of hart indices (not ids: see <arch/hart.h>),
 * CONFIG_PLATFORM_HART_COUNT bits.
 */

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

static inline void hartmask_set(struct hartmask *m, unsigned int index)
{
	bit_set(m->bits, index);
}

static inline void hartmask_clear(struct hartmask *m, unsigned int index)
{
	bit_clear(m->bits, index);
}

static inline bool hartmask_test(const struct hartmask *m, unsigned int index)
{
	return bit_test(m->bits, index);
}

/* Atomic variants, for a mask shared between harts. */
static inline void hartmask_clear_atomic(struct hartmask *m, unsigned int index)
{
	atomic_and_ulong(&m->bits[_bit_word(index)], ~_bit_mask(index));
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
