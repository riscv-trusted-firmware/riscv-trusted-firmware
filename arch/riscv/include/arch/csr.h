/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_CSR_H
#define ARCH_CSR_H

#include <compiler.h>
#include <util.h>

#if __RISCV_XLEN__ == 64
#define REGBYTES 8
#define REG_L ld
#define REG_S sd
#define XLEN_SHIFT 3
#else
#define REGBYTES 4
#define REG_L lw
#define REG_S sw
#define XLEN_SHIFT 2
#endif

/* mstatus */
#define MSTATUS_MIE BIT(3)
#define MSTATUS_MPIE BIT(7)
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP SHIFT_UL(3, MSTATUS_MPP_SHIFT)
#define MSTATUS_FS GENMASK_UL(14, 13)
#define MSTATUS_MPRV BIT(17)
#define MSTATUS_TVM BIT(20)
#define MSTATUS_TW BIT(21)
#define MSTATUS_TSR BIT(22)

#define PRV_U 0
#define PRV_S 1
#define PRV_M 3

/* mie / mip */
#define MIP_SSIP BIT(1)
#define MIP_MSIP BIT(3)
#define MIP_STIP BIT(5)
#define MIP_MTIP BIT(7)
#define MIP_SEIP BIT(9)
#define MIP_MEIP BIT(11)

/* mcause exception codes */
#define CAUSE_MISALIGNED_FETCH 0
#define CAUSE_FETCH_ACCESS 1
#define CAUSE_ILLEGAL_INSN 2
#define CAUSE_BREAKPOINT 3
#define CAUSE_MISALIGNED_LOAD 4
#define CAUSE_LOAD_ACCESS 5
#define CAUSE_MISALIGNED_STORE 6
#define CAUSE_STORE_ACCESS 7
#define CAUSE_USER_ECALL 8
#define CAUSE_SUPERVISOR_ECALL 9
#define CAUSE_MACHINE_ECALL 11
#define CAUSE_FETCH_PAGE_FAULT 12
#define CAUSE_LOAD_PAGE_FAULT 13
#define CAUSE_STORE_PAGE_FAULT 15

#define CAUSE_IRQ_FLAG BIT(__RISCV_XLEN__ - 1)

#ifndef __ASSEMBLY__

#define csr_read(csr)                                        \
	({                                                   \
		unsigned long __v;                           \
		__asm__ __volatile__("csrr %0, " TO_STR(csr) \
				     : "=r"(__v)             \
				     :                       \
				     : "memory");            \
		__v;                                         \
	})

#define csr_write(csr, val)                                     \
	({                                                      \
		unsigned long __v = (unsigned long)(val);       \
		__asm__ __volatile__("csrw " TO_STR(csr) ", %0" \
				     :                          \
				     : "rK"(__v)                \
				     : "memory");               \
	})

#define csr_set(csr, val)                                       \
	({                                                      \
		unsigned long __v = (unsigned long)(val);       \
		__asm__ __volatile__("csrs " TO_STR(csr) ", %0" \
				     :                          \
				     : "rK"(__v)                \
				     : "memory");               \
	})

#define csr_clear(csr, val)                                     \
	({                                                      \
		unsigned long __v = (unsigned long)(val);       \
		__asm__ __volatile__("csrc " TO_STR(csr) ", %0" \
				     :                          \
				     : "rK"(__v)                \
				     : "memory");               \
	})

static inline void wfi(void)
{
	__asm__ __volatile__("wfi" ::: "memory");
}

#endif /* !__ASSEMBLY__ */

#endif
