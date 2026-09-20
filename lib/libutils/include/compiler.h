/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef COMPILER_H
#define COMPILER_H

/*
 * What is asked of the compiler rather than written in C: attributes,
 * builtins, and the few things that depend on which compiler it is. GCC
 * and clang are the compilers; both know everything used here.
 */

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

/* How a function or an object is to be treated. */
#define __deprecated __attribute__((deprecated))
#define __packed __attribute__((packed))
#define __weak __attribute__((weak))
#define __alias(x) __attribute__((alias(x)))
#define __noreturn __attribute__((noreturn))
#define __pure __attribute__((pure))
#define __attr_const __attribute__((const))
#define __aligned(x) __attribute__((aligned(x)))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))
#define __unused __attribute__((unused))
#define __maybe_unused __attribute__((unused))
#define __used __attribute__((used))
#define __must_check __attribute__((warn_unused_result))
#define __cold __attribute__((cold))
#define __noprof __attribute__((no_instrument_function))

/* Where it goes. */
#define __section(x) __attribute__((section(x)))
#define __data __section(".data")
#define __bss __section(".bss")
#define __rodata __section(".rodata")

#if __has_attribute(no_stack_protector)
#define __no_stack_protector __attribute__((no_stack_protector))
#else
#define __no_stack_protector
#endif

/* A case that runs into the next one on purpose. */
#if __has_attribute(fallthrough)
#define fallthrough __attribute__((fallthrough))
#else
#define fallthrough \
	do {        \
	} while (0) /* fallthrough */
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* The compiler may assume nothing about memory across this point. */
#define barrier() __asm__ __volatile__("" ::: "memory")

#define __compiler_bswap64(x) __builtin_bswap64((x))
#define __compiler_bswap32(x) __builtin_bswap32((x))
#define __compiler_bswap16(x) __builtin_bswap16((x))

/*
 * Arithmetic that says whether it overflowed: true when the result did not
 * fit *res, which then holds it truncated. See ADD_OVERFLOW() and friends
 * in <util.h>.
 */
#define __compiler_add_overflow(a, b, res) \
	__builtin_add_overflow((a), (b), (res))
#define __compiler_sub_overflow(a, b, res) \
	__builtin_sub_overflow((a), (b), (res))
#define __compiler_mul_overflow(a, b, res) \
	__builtin_mul_overflow((a), (b), (res))

/*
 * The atomics <atomic.h> is made of. A compare and swap that fails leaves
 * what it found in *oval. Sequentially consistent throughout: the harts of
 * the monitor talk to each other through these and little else.
 */
#define __compiler_compare_and_swap(p, oval, nval)              \
	__atomic_compare_exchange_n((p), (oval), (nval), false, \
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define __compiler_atomic_load(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define __compiler_atomic_store(p, val) \
	__atomic_store_n((p), (val), __ATOMIC_SEQ_CST)
#define __compiler_atomic_swap(p, val) \
	__atomic_exchange_n((p), (val), __ATOMIC_SEQ_CST)
#define __compiler_atomic_add(p, val) \
	__atomic_add_fetch((p), (val), __ATOMIC_SEQ_CST)
#define __compiler_atomic_sub(p, val) \
	__atomic_sub_fetch((p), (val), __ATOMIC_SEQ_CST)
#define __compiler_atomic_or(p, val) \
	__atomic_or_fetch((p), (val), __ATOMIC_SEQ_CST)
#define __compiler_atomic_and(p, val) \
	__atomic_and_fetch((p), (val), __ATOMIC_SEQ_CST)

#endif
