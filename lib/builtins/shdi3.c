// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * 64-bit shifts for RV32: when optimising for size the compiler calls
 * these instead of expanding a variable shift inline. They work on 32-bit
 * halves, so they cannot end up calling themselves.
 */

#include <stdint.h>

uint64_t __ashldi3(uint64_t v, int n);
uint64_t __lshrdi3(uint64_t v, int n);
int64_t __ashrdi3(int64_t v, int n);

/* RISC-V is little-endian. */
union halves {
	uint64_t whole;
	struct {
		uint32_t lo;
		uint32_t hi;
	};
};

uint64_t __ashldi3(uint64_t v, int n)
{
	union halves in = { .whole = v }, out = {};

	if (n == 0)
		return v;
	if (n >= 32) {
		out.lo = 0;
		out.hi = in.lo << (n - 32);
	} else {
		out.lo = in.lo << n;
		out.hi = (in.hi << n) | (in.lo >> (32 - n));
	}
	return out.whole;
}

uint64_t __lshrdi3(uint64_t v, int n)
{
	union halves in = { .whole = v }, out = {};

	if (n == 0)
		return v;
	if (n >= 32) {
		out.hi = 0;
		out.lo = in.hi >> (n - 32);
	} else {
		out.hi = in.hi >> n;
		out.lo = (in.lo >> n) | (in.hi << (32 - n));
	}
	return out.whole;
}

int64_t __ashrdi3(int64_t v, int n)
{
	union halves in = { .whole = (uint64_t)v }, out = {};

	if (n == 0)
		return v;
	if (n >= 32) {
		out.hi = (uint32_t)((int32_t)in.hi >> 31);
		out.lo = (uint32_t)((int32_t)in.hi >> (n - 32));
	} else {
		out.hi = (uint32_t)((int32_t)in.hi >> n);
		out.lo = (in.lo >> n) | (in.hi << (32 - n));
	}
	return (int64_t)out.whole;
}
