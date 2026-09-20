/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBITEST_SBICALL_H
#define SBITEST_SBICALL_H

#include <sbi/sbi.h>

struct sbiret {
	long error;
	long value;
};

static inline struct sbiret sbi_call(unsigned long eid, unsigned long fid,
				     unsigned long arg0, unsigned long arg1,
				     unsigned long arg2, unsigned long arg3,
				     unsigned long arg4)
{
	register unsigned long a0 __asm__("a0") = arg0;
	register unsigned long a1 __asm__("a1") = arg1;
	register unsigned long a2 __asm__("a2") = arg2;
	register unsigned long a3 __asm__("a3") = arg3;
	register unsigned long a4 __asm__("a4") = arg4;
	register unsigned long a6 __asm__("a6") = fid;
	register unsigned long a7 __asm__("a7") = eid;

	__asm__ __volatile__("ecall"
			     : "+r"(a0), "+r"(a1)
			     : "r"(a2), "r"(a3), "r"(a4), "r"(a6), "r"(a7)
			     : "memory");
	return (struct sbiret){ (long)a0, (long)a1 };
}

#define sbi_call0(eid, fid) sbi_call(eid, fid, 0, 0, 0, 0, 0)
#define sbi_call1(eid, fid, a) sbi_call(eid, fid, a, 0, 0, 0, 0)
#define sbi_call2(eid, fid, a, b) sbi_call(eid, fid, a, b, 0, 0, 0)
#define sbi_call3(eid, fid, a, b, c) sbi_call(eid, fid, a, b, c, 0, 0)

#endif
