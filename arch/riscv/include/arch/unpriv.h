/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_UNPRIV_H
#define ARCH_UNPRIV_H

/*
 * Access memory the way the trapping context would: with its privilege
 * level and address translation (mstatus.MPRV). A fault does not reach the
 * trapping context; it is reported to the caller, which usually forwards
 * it with trap_redirect().
 */

#include <arch/csr.h>
#include <arch/hart.h>
#include <arch/trap.h>
#include <util.h>

/*
 * Around the access instructions: mstatus as the trapping context left it
 * (its MPP and MPV, SUM and MXR, plus 'extra'), MPRV not yet set, and a
 * trap expected. On RV32 MPV is in mstatush, where the trap of a faulting
 * access clears it; the context is a guest's all the same, to return to
 * and to report the fault for.
 */
struct unpriv_window {
	unsigned long mstatus;
#if __RISCV_XLEN__ == 32
	unsigned long mstatush;
#endif
};

static inline void unpriv_begin(const struct trap_regs *regs,
				unsigned long extra, struct unpriv_window *w)
{
	struct hart *h = this_hart();

	w->mstatus = csr_read(mstatus);
#if __RISCV_XLEN__ == 32
	w->mstatush = hart_has(HART_FEAT_H) ? csr_read(CSR_MSTATUSH) : 0;
#endif
	h->trap_taken = 0;
	h->trap_expected = 1;
	csr_write(mstatus, (regs->mstatus & ~MSTATUS_MPRV) | extra);
}

/*
 * false: the access faulted, and *fault is what the trapping context's side
 * gets to see.
 */
static inline bool unpriv_end(const struct trap_regs *regs,
			      const struct unpriv_window *w,
			      struct trap_info *fault)
{
	struct hart *h = this_hart();

	csr_write(mstatus, w->mstatus);
	h->trap_expected = 0;
	if (!h->trap_taken)
		return true;

	*fault = (struct trap_info){ .cause = h->trap_cause,
				     .tval = h->trap_tval };
	if (!hart_has(HART_FEAT_H))
		return false;
#if __RISCV_XLEN__ == 32
	csr_write(CSR_MSTATUSH, w->mstatush);
	fault->virt = w->mstatush & MSTATUSH_MPV;
#else
	fault->virt = regs->mstatus & MSTATUS_MPV;
#endif
	fault->gva = h->trap_gva;
	fault->tval2 = h->trap_tval2;
	/*
	 * mtinst is about the monitor's access instruction, not the guest's.
	 * What says that the fault was in an implicit access of the VS-stage
	 * translation is about the address, and the hypervisor needs it.
	 */
	if ((h->trap_tinst & ~UL(0x1020)) == 0x2000)
		fault->tinst = h->trap_tinst;
	return false;
}

/* Read a 1/2/4/8-byte value (8 on RV64 only). false: faulted, *fault is set. */
bool unpriv_read(const struct trap_regs *regs, unsigned long addr,
		 unsigned int width, unsigned long *val,
		 struct trap_info *fault);

bool unpriv_write_byte(const struct trap_regs *regs, unsigned long addr,
		       uint8_t val, struct trap_info *fault);

/* Fetch the instruction at regs->mepc (16 or 32 bits wide). */
bool unpriv_fetch_insn(const struct trap_regs *regs, unsigned long *insn,
		       struct trap_info *fault);

#endif
