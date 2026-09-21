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
#define MSTATUS_SIE BIT(1)
#define MSTATUS_MIE BIT(3)
#define MSTATUS_SPIE BIT(5)
#define MSTATUS_MPIE BIT(7)
#define MSTATUS_SPP BIT(8)
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP SHIFT_UL(3, MSTATUS_MPP_SHIFT)
#define MSTATUS_FS GENMASK_UL(14, 13)
#define MSTATUS_MPRV BIT(17)
#define MSTATUS_MXR BIT(19)
#define MSTATUS_TVM BIT(20)
#define MSTATUS_TW BIT(21)
#define MSTATUS_TSR BIT(22)
#define MSTATUS_SDT BIT(24) /* Ssdbltrp: S-mode disable trap */
/* Hypervisor extension: in mstatus on RV64, in mstatush on RV32. */
#if __RISCV_XLEN__ == 64
#define MSTATUS_GVA BIT(38)
#define MSTATUS_MPV BIT(39)
#else
#define MSTATUSH_GVA BIT(6)
#define MSTATUSH_MPV BIT(7)
#endif
/* Smdbltrp: M-mode disable trap. mstatus bit 42, mstatush bit 10. */
#define MSTATUS_MDT_SHIFT 42
#define MSTATUSH_MDT_SHIFT 10

#define PRV_U 0
#define PRV_S 1
#define PRV_M 3

/* hstatus */
#define HSTATUS_GVA BIT(6)
#define HSTATUS_SPV BIT(7)
#define HSTATUS_SPVP BIT(8)

/* mie / mip */
#define IRQ_S_SOFT 1
#define IRQ_VS_SOFT 2
#define IRQ_M_SOFT 3
#define IRQ_S_TIMER 5
#define IRQ_VS_TIMER 6
#define IRQ_M_TIMER 7
#define IRQ_S_EXT 9
#define IRQ_VS_EXT 10
#define IRQ_M_EXT 11
#define IRQ_S_GEXT 12
#define IRQ_PMU_OVF 13
/* AIA: local interrupts above the standard ones, in mieh and miph on RV32. */
#define IRQ_RAS_LOW 35
#define IRQ_RAS_HIGH 43

#define MIP_SSIP BIT(IRQ_S_SOFT)
#define MIP_MSIP BIT(IRQ_M_SOFT)
#define MIP_STIP BIT(IRQ_S_TIMER)
#define MIP_MTIP BIT(IRQ_M_TIMER)
#define MIP_SEIP BIT(IRQ_S_EXT)
#define MIP_MEIP BIT(IRQ_M_EXT)

/* misa */
#define MISA_EXT(c) BIT((c) - 'A')

/* menvcfg (the upper half is menvcfgh on RV32) */
#define ENVCFG_LPE_BIT 2
#define ENVCFG_SSE_BIT 3
#define ENVCFG_CBIE GENMASK_UL(5, 4)
#define ENVCFG_CBCFE BIT(6)
#define ENVCFG_CBZE BIT(7)
#define ENVCFG_PMM_SHIFT 32 /* 2 bits: 0 off, 2 PMLEN 7, 3 PMLEN 16 */
#define ENVCFG_DTE_BIT 59
#define ENVCFG_CDE_BIT 60 /* Smcdeleg: counter delegation */
#define ENVCFG_ADUE_BIT 61
#define ENVCFG_PBMTE_BIT 62
#define ENVCFG_STCE_BIT 63

/* mhpmevent (Sscofpmf) */
#define HPMEVENT_OF_BIT 63
#define HPMEVENT_MINH_BIT 62
#define HPMEVENT_SINH_BIT 61
#define HPMEVENT_UINH_BIT 60
#define HPMEVENT_VSINH_BIT 59
#define HPMEVENT_VUINH_BIT 58

/* mcounteren */
#define COUNTEREN_TM BIT(1)

/* pmpcfg */
#define PMP_R 0x01
#define PMP_W 0x02
#define PMP_X 0x04
#define PMP_A_TOR 0x08
#define PMP_A_NA4 0x10
#define PMP_A_NAPOT 0x18
#define PMP_L 0x80

/*
 * CSRs that older assemblers do not know by name, or only accept with a
 * matching -march, are addressed by number.
 */
#define CSR_STIMECMP 0x14d
#define CSR_STIMECMPH 0x15d
#define CSR_MENVCFG 0x30a
#define CSR_SENVCFG 0x10a
#define CSR_MSTATEEN0 0x30c /* + n, n = 0..3 */
#define CSR_MSTATEEN0H 0x31c
#define CSR_MENVCFGH 0x31a
#define CSR_MIEH 0x314
#define CSR_MSTATUSH 0x310
#define CSR_HSTATUS 0x600
#define CSR_TSELECT 0x7a0
#define CSR_TDATA1 0x7a1
#define CSR_TDATA2 0x7a2
#define CSR_TDATA3 0x7a3
#define CSR_TINFO 0x7a4
#define CSR_HTVAL 0x643
#define CSR_HTINST 0x64a
#define CSR_MTINST 0x34a
#define CSR_MTVAL2 0x34b
#define CSR_PMPCFG0 0x3a0
#define CSR_PMPADDR0 0x3b0
#define CSR_MCOUNTINHIBIT 0x320
#define CSR_MCYCLECFG 0x321 /* Smcntrpmf */
#define CSR_MINSTRETCFG 0x322
#define CSR_MCYCLECFGH 0x721
#define CSR_MINSTRETCFGH 0x722
#define CSR_MHPMEVENT0 0x320 /* + n, n = 3..31 */
#define CSR_MHPMEVENT0H 0x720
#define CSR_MCYCLE 0xb00 /* mhpmcounter n = + n */
#define CSR_MCYCLEH 0xb80
#define CSR_SCOUNTOVF 0xda0
#define CSR_TIME 0xc01
#define CSR_TIMEH 0xc81

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
#define CAUSE_VSUPERVISOR_ECALL 10
#define CAUSE_MACHINE_ECALL 11
#define CAUSE_FETCH_PAGE_FAULT 12
#define CAUSE_LOAD_PAGE_FAULT 13
#define CAUSE_STORE_PAGE_FAULT 15
#define CAUSE_DOUBLE_TRAP 16
#define CAUSE_FETCH_GUEST_PAGE_FAULT 20
#define CAUSE_LOAD_GUEST_PAGE_FAULT 21
#define CAUSE_VIRTUAL_INSN 22
#define CAUSE_STORE_GUEST_PAGE_FAULT 23

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

#define csr_swap(csr, val)                                           \
	({                                                           \
		unsigned long __new = (unsigned long)(val), __old;   \
		__asm__ __volatile__("csrrw %0, " TO_STR(csr) ", %1" \
				     : "=r"(__old)                   \
				     : "r"(__new)                    \
				     : "memory");                    \
		__old;                                               \
	})

static inline void wfi(void)
{
	__asm__ __volatile__("wfi" ::: "memory");
}

static inline void cpu_relax(void)
{
	/* PAUSE (Zihintpause); a no-op hint where it is not implemented. */
	__asm__ __volatile__(".4byte 0x0100000f" ::: "memory");
}

#endif /* !__ASSEMBLY__ */

#endif
