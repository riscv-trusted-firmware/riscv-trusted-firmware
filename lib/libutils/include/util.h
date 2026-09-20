/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef UTIL_H
#define UTIL_H

/*
 * Small things everybody needs: bits, masks and fields, rounding, sizes,
 * minimum and maximum, arithmetic that notices an overflow. Bits, shifts
 * and sizes are usable from assembly and linker scripts as well, where a
 * constant has no type and no suffix.
 */

#include <compiler.h>

#ifdef __ASSEMBLER__

#define U(x) x
#define UL(x) x
#define ULL(x) x

#define BIT32(nr) (1 << (nr))
#define BIT64(nr) (1 << (nr))
#define BIT(nr) (1 << (nr))
#define SHIFT_U32(v, shift) ((v) << (shift))
#define SHIFT_U64(v, shift) ((v) << (shift))
#define SHIFT_UL(v, shift) ((v) << (shift))
#define GENMASK_32(h, l) (((1 << ((h) - (l) + 1)) - 1) << (l))
#define GENMASK_64(h, l) (((1 << ((h) - (l) + 1)) - 1) << (l))
#define GENMASK_UL(h, l) (((1 << ((h) - (l) + 1)) - 1) << (l))

#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define U(x) x##U
#define UL(x) x##UL
#define ULL(x) x##ULL

/*
 * One bit, as the type says. BIT() is a register's bit: as wide as the
 * registers are, which is what a CSR and most of what the monitor keeps
 * bits in want.
 */
#define BIT32(nr) (UINT32_C(1) << (nr))
#define BIT64(nr) (UINT64_C(1) << (nr))
#define BIT(nr) (1UL << (nr))

/* A value moved into place, having the type first so that nothing falls off. */
#define SHIFT_U32(v, shift) ((uint32_t)(v) << (shift))
#define SHIFT_U64(v, shift) ((uint64_t)(v) << (shift))
#define SHIFT_UL(v, shift) ((unsigned long)(v) << (shift))

/* Bits h down to l, both included. */
#define GENMASK_32(h, l) \
	(((~UINT32_C(0)) << (l)) & (~UINT32_C(0) >> (32 - 1 - (h))))
#define GENMASK_64(h, l) \
	(((~UINT64_C(0)) << (l)) & (~UINT64_C(0) >> (64 - 1 - (h))))
#define GENMASK_UL(h, l) \
	(((~0UL) << (l)) & (~0UL >> (8 * sizeof(long) - 1 - (h))))

#endif /* __ASSEMBLER__ */

#define SIZE_4K UL(0x1000)
#define SIZE_1M UL(0x100000)
#define SIZE_2M UL(0x200000)
#define SIZE_4M UL(0x400000)
#define SIZE_8M UL(0x800000)
#define SIZE_2G UL(0x80000000)

#ifndef __ASSEMBLER__

/* Each argument evaluated once, and only comparable types compared. */
#define __MAX(a, b, ua, ub)             \
	(__extension__({                \
		__typeof__(a) ua = (a); \
		__typeof__(b) ub = (b); \
		(void)(&ua == &ub);     \
		ua > ub ? ua : ub;      \
	}))
#define __MIN(a, b, ua, ub)             \
	(__extension__({                \
		__typeof__(a) ua = (a); \
		__typeof__(b) ub = (b); \
		(void)(&ua == &ub);     \
		ua < ub ? ua : ub;      \
	}))
#define MAX(a, b)                                     \
	__MAX((a), (b), CONCAT(__max_a, __COUNTER__), \
	      CONCAT(__max_b, __COUNTER__))
#define MIN(a, b)                                     \
	__MIN((a), (b), CONCAT(__min_a, __COUNTER__), \
	      CONCAT(__min_b, __COUNTER__))

/* For where a statement expression cannot go: constants, array sizes. */
#define MAX_UNSAFE(a, b) (((a) > (b)) ? (a) : (b))
#define MIN_UNSAFE(a, b) (((a) < (b)) ? (a) : (b))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * Rounding to a multiple of y, in the type of x. The plain ones take any
 * y and cost a division; the ones with a 2 want a power of two and cost
 * a mask. ROUNDUP_OVERFLOW() says whether the result fit (false: it did).
 */
#define ROUNDUP(x, y)                                            \
	((((x) + (__typeof__(x))(y) - 1) / (__typeof__(x))(y)) * \
	 (__typeof__(x))(y))
#define ROUNDDOWN(x, y) (((x) / (__typeof__(x))(y)) * (__typeof__(x))(y))
#define ROUNDUP2(x, y) \
	(((x) + (__typeof__(x))(y) - 1) & ~((__typeof__(x))(y) - 1))
#define ROUNDDOWN2(x, y) ((x) & ~((__typeof__(x))(y) - 1))
#define ROUNDUP_OVERFLOW(x, y, res)                                            \
	(__extension__({                                                       \
		__typeof__(x) __roundup_tmp = 0;                               \
		__typeof__(y) __roundup_mask = (y) - 1;                        \
		ADD_OVERFLOW((x), __roundup_mask, &__roundup_tmp) ?            \
			1 :                                                    \
			((void)(*(res) = __roundup_tmp & ~__roundup_mask), 0); \
	}))

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))
#define ROUNDUP_DIV(x, y) (ROUNDUP((x), (y)) / (__typeof__(x))(y))
/* Unsigned division to the nearest: half a divisor added first. */
#define UDIV_ROUND_NEAREST(x, y) (((x) + ((y) / 2)) / (y))

/* Zero is no power of two. */
#define IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & (~(x) + 1)) == (x)))
/* To a power of two, that is. */
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
#define IS_ALIGNED_WITH_TYPE(x, type)                                    \
	(__extension__({                                                 \
		type __is_aligned_y;                                     \
		IS_ALIGNED((uintptr_t)(x), __alignof__(__is_aligned_y)); \
	}))

#define container_of(ptr, type, member)                                    \
	(__extension__({                                                   \
		const __typeof__(((type *)0)->member) *__ptr = (ptr);      \
		(type *)((unsigned long)(__ptr) - offsetof(type, member)); \
	}))
#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

/* true when the result did not fit *res. */
#define ADD_OVERFLOW(a, b, res) __compiler_add_overflow((a), (b), (res))
#define SUB_OVERFLOW(a, b, res) __compiler_sub_overflow((a), (b), (res))
#define MUL_OVERFLOW(a, b, res) __compiler_mul_overflow((a), (b), (res))

/* -1, 0 or 1: a less than, equal to, greater than b. */
#define CMP_TRILEAN(a, b)                                           \
	(__extension__({                                            \
		__typeof__(a) __cmp_a = (a);                        \
		__typeof__(b) __cmp_b = (b);                        \
		__cmp_a > __cmp_b ? 1 : __cmp_a < __cmp_b ? -1 : 0; \
	}))

/*
 * 64 bits in two words of 32, the upper one first: how an address or a
 * size travels in a device tree cell pair, an RPMI message, a pair of SBI
 * arguments on RV32.
 */
static inline uint64_t reg_pair_to_64(uint32_t reg0, uint32_t reg1)
{
	return (uint64_t)reg0 << 32 | reg1;
}

static inline uint32_t high32_from_64(uint64_t val)
{
	return (uint32_t)(val >> 32);
}

static inline uint32_t low32_from_64(uint64_t val)
{
	return (uint32_t)val;
}

static inline void reg_pair_from_64(uint64_t val, uint32_t *reg0,
				    uint32_t *reg1)
{
	*reg0 = high32_from_64(val);
	*reg1 = low32_from_64(val);
}

/*
 * A field of a register, given by its mask: the value it holds, moved down
 * to bit 0, and the register with another value put there. The mask says
 * where the field is and how wide; a value wider than that is cut.
 */
static inline uint32_t get_field_u32(uint32_t reg, uint32_t mask)
{
	return (reg & mask) / (mask & ~(mask - 1));
}

static inline uint32_t set_field_u32(uint32_t reg, uint32_t mask, uint32_t val)
{
	return (reg & ~mask) | ((val * (mask & ~(mask - 1))) & mask);
}

static inline uint64_t get_field_u64(uint64_t reg, uint64_t mask)
{
	return (reg & mask) / (mask & ~(mask - 1));
}

static inline uint64_t set_field_u64(uint64_t reg, uint64_t mask, uint64_t val)
{
	return (reg & ~mask) | ((val * (mask & ~(mask - 1))) & mask);
}

static inline unsigned long get_field_ul(unsigned long reg, unsigned long mask)
{
	return (reg & mask) / (mask & ~(mask - 1));
}

static inline unsigned long set_field_ul(unsigned long reg, unsigned long mask,
					 unsigned long val)
{
	return (reg & ~mask) | ((val * (mask & ~(mask - 1))) & mask);
}

#endif /* !__ASSEMBLER__ */

/* An argument as a string, macros in it expanded; two of them as one token. */
#define TO_STR(x) _TO_STR(x)
#define _TO_STR(x) #x
#define CONCAT(x, y) _CONCAT(x, y)
#define _CONCAT(x, y) x##y

#endif
