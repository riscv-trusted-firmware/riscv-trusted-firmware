/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef LIMITS_H
#define LIMITS_H

#include <util.h>

/* The compiler's own limits.h expects a hosted libc next to it. */

#define CHAR_BIT 8
#define SCHAR_MAX __SCHAR_MAX__
#define SCHAR_MIN (-SCHAR_MAX - 1)
#define UCHAR_MAX (SCHAR_MAX * 2 + 1)
#define SHRT_MAX __SHRT_MAX__
#define SHRT_MIN (-SHRT_MAX - 1)
#define USHRT_MAX (SHRT_MAX * 2 + 1)
#define INT_MAX __INT_MAX__
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX (INT_MAX * U(2) + U(1))
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (LONG_MAX * UL(2) + UL(1))
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (LLONG_MAX * ULL(2) + ULL(1))

#endif
