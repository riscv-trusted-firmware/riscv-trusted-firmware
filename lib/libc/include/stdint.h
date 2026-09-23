/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The fixed-width integers, with one choice the compiler's own header
 * leaves to the target: int32_t and uint32_t are int and unsigned int
 * here on every target, so that "%u" and "%x" print them everywhere
 * (some bare-metal toolchains make them long on RV32). The other widths
 * are the compiler's.
 */

#ifndef STDINT_H
#define STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;

typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef uint8_t uint_fast8_t;
typedef int16_t int_fast16_t;
typedef uint16_t uint_fast16_t;
typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;

typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

#define INT8_MIN	(-128)
#define INT8_MAX	127
#define UINT8_MAX	255
#define INT16_MIN	(-32768)
#define INT16_MAX	32767
#define UINT16_MAX	65535
#define INT32_MIN	(-2147483647 - 1)
#define INT32_MAX	2147483647
#define UINT32_MAX	4294967295U
#define INT64_MIN	(-__INT64_MAX__ - 1)
#define INT64_MAX	__INT64_MAX__
#define UINT64_MAX	__UINT64_MAX__

#define INT_LEAST8_MIN		INT8_MIN
#define INT_LEAST8_MAX		INT8_MAX
#define UINT_LEAST8_MAX		UINT8_MAX
#define INT_LEAST16_MIN		INT16_MIN
#define INT_LEAST16_MAX		INT16_MAX
#define UINT_LEAST16_MAX	UINT16_MAX
#define INT_LEAST32_MIN		INT32_MIN
#define INT_LEAST32_MAX		INT32_MAX
#define UINT_LEAST32_MAX	UINT32_MAX
#define INT_LEAST64_MIN		INT64_MIN
#define INT_LEAST64_MAX		INT64_MAX
#define UINT_LEAST64_MAX	UINT64_MAX

#define INT_FAST8_MIN		INT8_MIN
#define INT_FAST8_MAX		INT8_MAX
#define UINT_FAST8_MAX		UINT8_MAX
#define INT_FAST16_MIN		INT16_MIN
#define INT_FAST16_MAX		INT16_MAX
#define UINT_FAST16_MAX		UINT16_MAX
#define INT_FAST32_MIN		INT32_MIN
#define INT_FAST32_MAX		INT32_MAX
#define UINT_FAST32_MAX		UINT32_MAX
#define INT_FAST64_MIN		INT64_MIN
#define INT_FAST64_MAX		INT64_MAX
#define UINT_FAST64_MAX		UINT64_MAX

#define INTPTR_MIN	(-__INTPTR_MAX__ - 1)
#define INTPTR_MAX	__INTPTR_MAX__
#define UINTPTR_MAX	__UINTPTR_MAX__
#define INTMAX_MIN	(-__INTMAX_MAX__ - 1)
#define INTMAX_MAX	__INTMAX_MAX__
#define UINTMAX_MAX	__UINTMAX_MAX__
#define PTRDIFF_MIN	(-__PTRDIFF_MAX__ - 1)
#define PTRDIFF_MAX	__PTRDIFF_MAX__
#define SIZE_MAX	__SIZE_MAX__
#define SIG_ATOMIC_MIN	INT32_MIN
#define SIG_ATOMIC_MAX	INT32_MAX
#define WCHAR_MIN	__WCHAR_MIN__
#define WCHAR_MAX	__WCHAR_MAX__
#define WINT_MIN	__WINT_MIN__
#define WINT_MAX	__WINT_MAX__

#define INT8_C(c)	c
#define UINT8_C(c)	c
#define INT16_C(c)	c
#define UINT16_C(c)	c
#define INT32_C(c)	c
#define UINT32_C(c)	c ## U
#if __RISCV_XLEN__ == 64
#define INT64_C(c)	c ## L
#define UINT64_C(c)	c ## UL
#else
#define INT64_C(c)	c ## LL
#define UINT64_C(c)	c ## ULL
#endif
#define INTMAX_C(c)	INT64_C(c)
#define UINTMAX_C(c)	UINT64_C(c)

#endif
