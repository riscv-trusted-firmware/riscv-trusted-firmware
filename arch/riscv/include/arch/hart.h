/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_HART_H
#define ARCH_HART_H

/*
 * Per-hart monitor state.
 *
 * In M-mode, tp points to the running hart's struct hart and mscratch is 0.
 * While a lower privilege level runs, mscratch holds the pointer; the trap
 * entry swaps it back into tp and switches to the hart's M-mode stack.
 */

#include <arch/csr.h>
#include <types_ext.h>

/* Offsets used by trap_entry.S and switch.S. */
#define HART_M_SP (0 * REGBYTES)
#define HART_TMP (1 * REGBYTES)

#ifndef __ASSEMBLY__

#include <atomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hart {
	unsigned long m_sp; /* top of the M-mode stack */
	unsigned long tmp; /* trap entry scratch slot */
	unsigned long hartid;
	unsigned long present; /* reached the monitor */

	/*
	 * A trap taken in M-mode is expected and skipped (CSR probing, unpriv).
	 */
	unsigned long trap_expected;
	unsigned long trap_taken;
	unsigned long trap_cause;
	unsigned long trap_tval;

	/* Hart state management. */
	unsigned long hsm_state; /* atomic, SBI_HSM_STATE_* */
	unsigned long start_addr;
	unsigned long start_arg;

	unsigned long ipi_pending; /* atomic, BIT(IPI_EVENT_*) */
};

enum hart_feature {
	HART_FEAT_PMP, /* at least two PMP entries */
	HART_FEAT_TIME_CSR, /* 'time' readable without trapping */
	HART_FEAT_MENVCFG, /* privileged spec 1.12 menvcfg */
	HART_FEAT_SSTC,
	HART_FEAT_SSCOFPMF,
	HART_FEAT_SMSTATEEN,
	HART_FEAT_H,
	HART_FEAT_COUNT,
};

register struct hart *__this_hart __asm__("tp");

static inline struct hart *this_hart(void)
{
	return __this_hart;
}

static inline unsigned long this_hartid(void)
{
	return this_hart()->hartid;
}

/* NULL when hartid is out of range. */
struct hart *hart_get(unsigned long hartid);
/* In range and running the monitor. */
bool hart_valid(unsigned long hartid);
unsigned int hart_count(void);

/* First C call on every hart: binds tp and the M-mode stack. */
void hart_init(unsigned long hartid);
/* Boot hart, once: probe the optional CSRs and extensions. */
void hart_detect_features(void);
bool hart_has(enum hart_feature feat);
/* Every hart: delegation, counters, envcfg, PMP, interrupt enables. */
void hart_runtime_init(void);

/*
 * Run a statement that may trap (an access to a CSR that may not exist):
 * false when it did. Every instruction that can trap must be 4 bytes long.
 */
#define may_trap(stmt)                                  \
	({                                              \
		struct hart *__h = this_hart();         \
		unsigned long __ms = csr_read(mstatus); \
		__h->trap_taken = 0;                    \
		__h->trap_expected = 1;                 \
		stmt;                                   \
		__h->trap_expected = 0;                 \
		csr_write(mstatus, __ms);               \
		!__h->trap_taken;                       \
	})

/* Read a CSR that may not exist: false when the access trapped. */
#define csr_probe(csr, valp) may_trap(*(valp) = csr_read(csr))

/* Is [addr, addr + size) memory the next stage may be given or may name? */
bool smode_range_ok(paddr_t addr, paddr_size_t size);

/* Reset S-mode CSR state and mret to 'entry' with (a0, a1) = (arg0, arg1). */
void __noreturn hart_enter_smode(unsigned long entry, unsigned long arg0,
				 unsigned long arg1);
/* Drop the current M-mode stack contents and continue in fn(). */
void __noreturn hart_restart_stack(void (*fn)(void));
void __noreturn hart_halt(void);

/* switch.S */
void __noreturn _hart_mret(unsigned long entry, unsigned long arg0,
			   unsigned long arg1);

#endif /* !__ASSEMBLY__ */

#endif
