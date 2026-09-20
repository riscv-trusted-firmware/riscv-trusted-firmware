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

struct domain;

struct hart {
	unsigned long m_sp; /* top of the M-mode stack */
	unsigned long tmp; /* trap entry scratch slot */
	unsigned long hartid;
	/* position in the hart table, see <boot.h> */
	unsigned int index;
	/* the domain it runs now, see <domain.h> */
	struct domain *domain;
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
	/* PRV_S, or PRV_U for a domain that asks for it */
	unsigned long start_mode;

	unsigned long ipi_pending; /* atomic, BIT(IPI_EVENT_*) */
};

enum hart_feature {
	HART_FEAT_PMP, /* at least two PMP entries */
	HART_FEAT_SMEPMP,
	HART_FEAT_TIME_CSR, /* 'time' readable without trapping */
	HART_FEAT_MENVCFG, /* privileged spec 1.12 menvcfg */
	HART_FEAT_SSTC,
	HART_FEAT_SSCOFPMF,
	HART_FEAT_SMSTATEEN,
	HART_FEAT_SDTRIG,
	HART_FEAT_SSDBLTRP,
	HART_FEAT_H,
	/* mcyclecfg, minstretcfg: privilege filters for cycle and instret */
	HART_FEAT_SMCNTRPMF,
	/* menvcfg.CDE: the counters can be S-mode's (Ssccfg) */
	HART_FEAT_SMCDELEG,
	/* the entropy source, which S-mode gets (mseccfg.SSEED) */
	HART_FEAT_ZKR,
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

/*
 * Hart ids are what the hardware and S-mode use, and can be anything. Inside
 * the monitor a hart goes by its index: per-hart storage, hart masks, IPI
 * targets. Ids are translated where they come in (the SBI calls, the
 * device tree) and go out (a6 of an SSE handler, RPMI messages).
 */
static inline unsigned int this_hart_index(void)
{
	return this_hart()->index;
}

/* By id: NULL when the monitor does not manage such a hart. */
struct hart *hart_get(unsigned long hartid);
/* By index: NULL beyond the last managed hart. */
struct hart *hart_by_index(unsigned int index);
/* -1 when the monitor does not manage such a hart. */
int hart_index(unsigned long hartid);
/* The id of hart 'index', known before that hart shows up. */
unsigned long hart_id_of(unsigned int index);
/* Managed, and it has reached the monitor. */
bool hart_valid(unsigned long hartid);
bool hart_index_valid(unsigned int index);
unsigned int hart_count(void);
/*
 * Harts in the hart table, there yet or not: what per-hart storage is sized by.
 */
unsigned int hart_table_size(void);

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

/*
 * The calling hart changes domain (<domain.h>): what the services keep for
 * S-mode per hart is per domain as well, and what of it lives in the hart
 * (counters, triggers, feature bits) is taken out before the change and
 * put back, or set up anew for a domain that has not run here, after it.
 */
/*
 * Boot hart, once the domains are known: the services' state, per domain and
 * hart.
 */
void hart_services_init(void);
void hart_services_switch_out(void);
void hart_services_switch_in(bool fresh);

/* Ssdbltrp, and S-mode has turned it on (FWFT DOUBLE_TRAP: menvcfg.DTE). */
bool hart_smode_double_trap_enabled(void);

/* Where the monitor runs: its link address unless it relocated itself. */
vaddr_t monitor_base(void);

/*
 * Is [addr, addr + size) memory the next stage may be given or may name?
 * Not the monitor's, and with domains (<domain.h>) memory the calling
 * hart's domain can read and write.
 */
bool smode_range_ok(paddr_t addr, paddr_size_t size);
/* The same for memory the monitor only reads. */
bool smode_range_readable(paddr_t addr, paddr_size_t size);
/* The same for an address S-mode is to run from. */
bool smode_entry_ok(paddr_t addr);
/* Does the range keep clear of what is the monitor's alone? */
bool monitor_range_clear(paddr_t addr, paddr_size_t size);

/*
 * Reset S-mode CSR state and mret to 'entry' in 'mode' (PRV_S or PRV_U) with
 * (a0, a1) = (arg0, arg1).
 */
void __noreturn hart_enter_smode(unsigned long entry, unsigned long arg0,
				 unsigned long arg1, unsigned long mode);
/* Drop the current M-mode stack contents and continue in fn(). */
void __noreturn hart_restart_stack(void (*fn)(void));
void __noreturn hart_halt(void);

/* switch.S */
void __noreturn _hart_mret(unsigned long entry, unsigned long arg0,
			   unsigned long arg1);

#endif /* !__ASSEMBLY__ */

#endif
