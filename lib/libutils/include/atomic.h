/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ATOMIC_H
#define ATOMIC_H

/*
 * Atomic operations, named for what they do and what they do it to:
 * atomic_load_u32(), atomic_cas_ulong(). An unsigned long is the type most
 * of the monitor's shared words have (a hart's state, its pending events,
 * a word of a hart mask), so that one has the full set.
 *
 * With the A extension the compiler's builtins become LR/SC or AMO
 * sequences. Without it there is one hart, which M-mode never preempts, and
 * plain accesses are all it takes.
 *
 *   atomic_load_T(p), atomic_store_T(p, val)
 *   atomic_cas_T(p, oval, nval)   compare and swap: true when *p was *oval
 *                                 and is nval now, else *oval is what it was
 *   atomic_swap_T(p, val)         the old value
 *   atomic_add_T(p, val), atomic_sub_T(p, val)       the new value
 *   atomic_or_T(p, val), atomic_and_T(p, val)        the new value
 *   atomic_inc32(p), atomic_dec32(p)                 the new value
 *
 * for T in int, uint, u32, ulong.
 */

#ifndef __ASSEMBLER__

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_RISCV_ISA_A

#define __ATOMIC_OPS(name, type)                                             \
	static inline type atomic_load_##name(const type *p)                 \
	{                                                                    \
		return __compiler_atomic_load(p);                            \
	}                                                                    \
	static inline void atomic_store_##name(type *p, type val)            \
	{                                                                    \
		__compiler_atomic_store(p, val);                             \
	}                                                                    \
	static inline bool atomic_cas_##name(type *p, type *oval, type nval) \
	{                                                                    \
		return __compiler_compare_and_swap(p, oval, nval);           \
	}                                                                    \
	static inline type atomic_swap_##name(type *p, type val)             \
	{                                                                    \
		return __compiler_atomic_swap(p, val);                       \
	}                                                                    \
	static inline type atomic_add_##name(type *p, type val)              \
	{                                                                    \
		return __compiler_atomic_add(p, val);                        \
	}                                                                    \
	static inline type atomic_sub_##name(type *p, type val)              \
	{                                                                    \
		return __compiler_atomic_sub(p, val);                        \
	}                                                                    \
	static inline type atomic_or_##name(type *p, type val)               \
	{                                                                    \
		return __compiler_atomic_or(p, val);                         \
	}                                                                    \
	static inline type atomic_and_##name(type *p, type val)              \
	{                                                                    \
		return __compiler_atomic_and(p, val);                        \
	}

#else /* !CONFIG_RISCV_ISA_A */

#define __ATOMIC_OPS(name, type)                                             \
	static inline type atomic_load_##name(const type *p)                 \
	{                                                                    \
		return *(const volatile type *)p;                            \
	}                                                                    \
	static inline void atomic_store_##name(type *p, type val)            \
	{                                                                    \
		*(volatile type *)p = val;                                   \
	}                                                                    \
	static inline bool atomic_cas_##name(type *p, type *oval, type nval) \
	{                                                                    \
		type cur = atomic_load_##name(p);                            \
									     \
		if (cur != *oval) {                                          \
			*oval = cur;                                         \
			return false;                                        \
		}                                                            \
		atomic_store_##name(p, nval);                                \
		return true;                                                 \
	}                                                                    \
	static inline type atomic_swap_##name(type *p, type val)             \
	{                                                                    \
		type old = atomic_load_##name(p);                            \
									     \
		atomic_store_##name(p, val);                                 \
		return old;                                                  \
	}                                                                    \
	static inline type atomic_add_##name(type *p, type val)              \
	{                                                                    \
		atomic_store_##name(p, (type)(atomic_load_##name(p) + val)); \
		return atomic_load_##name(p);                                \
	}                                                                    \
	static inline type atomic_sub_##name(type *p, type val)              \
	{                                                                    \
		atomic_store_##name(p, (type)(atomic_load_##name(p) - val)); \
		return atomic_load_##name(p);                                \
	}                                                                    \
	static inline type atomic_or_##name(type *p, type val)               \
	{                                                                    \
		atomic_store_##name(p, atomic_load_##name(p) | val);         \
		return atomic_load_##name(p);                                \
	}                                                                    \
	static inline type atomic_and_##name(type *p, type val)              \
	{                                                                    \
		atomic_store_##name(p, atomic_load_##name(p) & val);         \
		return atomic_load_##name(p);                                \
	}

#endif

__ATOMIC_OPS(int, int)
__ATOMIC_OPS(uint, unsigned int)
__ATOMIC_OPS(u32, uint32_t)
__ATOMIC_OPS(ulong, unsigned long)

static inline uint32_t atomic_inc32(uint32_t *p)
{
	return atomic_add_u32(p, 1);
}

static inline uint32_t atomic_dec32(uint32_t *p)
{
	return atomic_sub_u32(p, 1);
}

#endif /* !__ASSEMBLER__ */

#endif
