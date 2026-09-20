// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * 64-bit integer division for targets whose toolchain provides no
 * libgcc/compiler-rt (typically clang on RV32). Shift-subtract; correctness
 * over speed.
 */

#include <stdint.h>
#include <util.h>

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem);
uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);
int64_t __divdi3(int64_t n, int64_t d);
int64_t __moddi3(int64_t n, int64_t d);

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
	uint64_t q = 0, r = 0;

	if (d == 0) {
		if (rem)
			*rem = n;
		/* division by zero: mirror libgcc's undefined-ish result */
		return ~ULL(0);
	}
	for (int i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= BIT64(i);
		}
	}
	if (rem)
		*rem = r;
	return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d)
{
	return __udivmoddi4(n, d, 0);
}

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
	uint64_t r = 0;

	__udivmoddi4(n, d, &r);
	return r;
}

int64_t __divdi3(int64_t n, int64_t d)
{
	int neg = (n < 0) != (d < 0);
	uint64_t q = __udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n,
				  d < 0 ? -(uint64_t)d : (uint64_t)d, 0);

	return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t n, int64_t d)
{
	uint64_t r = 0;

	__udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n,
		     d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
	return n < 0 ? -(int64_t)r : (int64_t)r;
}
