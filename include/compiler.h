/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef COMPILER_H
#define COMPILER_H

#define __used __attribute__((used))
#define __unused __attribute__((unused))
#define __section(s) __attribute__((section(s)))
#define __aligned(n) __attribute__((aligned(n)))
#define __packed __attribute__((packed))
#define __noreturn __attribute__((noreturn))
#define __weak __attribute__((weak))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define barrier() __asm__ __volatile__("" ::: "memory")

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define BIT(n) (1UL << (n))

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

#endif
