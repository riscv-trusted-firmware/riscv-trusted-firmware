// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Illegal instruction handling: emulate what the hart is allowed not to
 * implement, hand everything else to S-mode.
 *
 *  - reads of the unprivileged counter CSRs the hart lacks: 'time' from
 *    the platform timer, 'cycle', 'instret' and 'hpmcounterN' from their
 *    machine-level counterparts;
 *  - the AMO instructions (Zaamo) on a hart that only has LR/SC (Zalrsc),
 *    when the firmware is built with the A extension.
 */

#include <arch/hart.h>
#include <arch/pmu.h>
#include <arch/trap.h>
#include <arch/unpriv.h>
#include <sbi/sbi.h>
#include <timer.h>
#include <util.h>

#define INSN_OPCODE(i) ((i) & 0x7f)
#define INSN_RD(i) (((i) >> 7) & 0x1f)
#define INSN_FUNCT3(i) (((i) >> 12) & 7)
#define INSN_RS1(i) (((i) >> 15) & 0x1f)
#define INSN_RS2(i) (((i) >> 20) & 0x1f)
#define INSN_CSR(i) (((i) >> 20) & 0xfff)
#define INSN_FUNCT5(i) (((i) >> 27) & 0x1f)

#define OPCODE_SYSTEM 0x73
#define OPCODE_AMO 0x2f
#define FUNCT3_CSRRS 2
#define FUNCT3_CSRRC 3
#define FUNCT3_CSRRSI 6
#define FUNCT3_CSRRCI 7

#define CSR_CYCLE 0xc00
#define CSR_CYCLEH 0xc80
#define CSR_SCOUNTEREN 0x106

/* A machine counter by number; false if the hart has no such counter. */
static bool mcounter_get(unsigned int n, uint64_t *val)
{
	unsigned long lo = 0, hi = 0;
	bool ok = false;

	switch (n) {
	case 0:
		ok = may_trap(lo = csr_read(CSR_MCYCLE));
#if __RISCV_XLEN__ == 32
		ok = ok && may_trap(hi = csr_read(CSR_MCYCLEH));
#endif
		break;
	case 2:
		ok = may_trap(lo = csr_read(CSR_MCYCLE + 2));
#if __RISCV_XLEN__ == 32
		ok = ok && may_trap(hi = csr_read(CSR_MCYCLEH + 2));
#endif
		break;
	default:
		return pmu_hw_counter_get(n, val);
	}
	*val = SHIFT_U64(hi, 32) | lo;
	return ok;
}

static bool emulate_csr_read(struct trap_regs *regs, unsigned long insn)
{
	unsigned int f3 = INSN_FUNCT3(insn), csr = INSN_CSR(insn), n = 0;
	bool from_u = !(regs->mstatus & MSTATUS_MPP);
	bool high = false;
	uint64_t val = 0;

	/* Read-only CSRs: only csrrs/csrrc[i] with a zero source are reads. */
	if (f3 != FUNCT3_CSRRS && f3 != FUNCT3_CSRRC && f3 != FUNCT3_CSRRSI &&
	    f3 != FUNCT3_CSRRCI)
		return false;
	if (INSN_RS1(insn) != 0)
		return false;

	if (csr >= CSR_CYCLE && csr < CSR_CYCLE + 32) {
		n = csr - CSR_CYCLE;
#if __RISCV_XLEN__ == 32
	} else if (csr >= CSR_CYCLEH && csr < CSR_CYCLEH + 32) {
		n = csr - CSR_CYCLEH;
		high = true;
#endif
	} else {
		return false;
	}

	/* A U-mode access S-mode has not opened is S-mode's trap to handle. */
	if (from_u && !(csr_read(CSR_SCOUNTEREN) & BIT(n)))
		return false;

	if (n == 1) {
		if (!timer_available())
			return false;
		val = timer_now();
	} else if (!mcounter_get(n, &val)) {
		return false;
	}

	if (INSN_RD(insn))
		*trap_reg(regs, INSN_RD(insn)) =
			(unsigned long)(high ? val >> 32 : val);
	regs->mepc += 4;
	return true;
}

#ifdef CONFIG_RISCV_ISA_A

/*
 * One attempt at "rd = *addr; *addr = op(*addr, src)" as an LR/SC pair, with
 * the privilege and translation of the trapping context. 0: done, 1: the
 * SC failed, try again. A fault shows in the hart's trap_taken.
 *
 * mstatus.MPRV is only set around the LR/SC pair itself: while it is set,
 * every load and store of M-mode goes by the trapping context's rules, the
 * compiler's jump tables and spills included.
 */
#define AMO_ATTEMPT(lr, sc, op)                                               \
	({                                                                    \
		__asm__ __volatile__(".option push\n.option norvc\n"          \
				     "csrs mstatus, %5\n" lr " %0, (%3)\n" op \
				     "\n" sc " %1, %2, (%3)\n"                \
				     "csrc mstatus, %5\n"                     \
				     ".option pop"                            \
				     : "=&r"(old), "=&r"(failed), "=&r"(new)  \
				     : "r"(addr), "r"(src), "r"(mprv)         \
				     : "memory");                             \
	})

static void amo_attempt_w(unsigned int funct5, unsigned long addr,
			  unsigned long src, unsigned long *old_out,
			  unsigned long *failed_out)
{
	const unsigned long mprv = MSTATUS_MPRV;
	unsigned long old = 0, new = 0, failed = 1;

	switch (funct5) {
	case 0x01:
		AMO_ATTEMPT("lr.w", "sc.w", "mv %2, %4");
		break;
	case 0x00:
		AMO_ATTEMPT("lr.w", "sc.w", "add %2, %0, %4");
		break;
	case 0x04:
		AMO_ATTEMPT("lr.w", "sc.w", "xor %2, %0, %4");
		break;
	case 0x0c:
		AMO_ATTEMPT("lr.w", "sc.w", "and %2, %0, %4");
		break;
	case 0x08:
		AMO_ATTEMPT("lr.w", "sc.w", "or %2, %0, %4");
		break;
	case 0x10:
		AMO_ATTEMPT("lr.w", "sc.w",
			    "mv %2, %0\nblt %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x14:
		AMO_ATTEMPT("lr.w", "sc.w",
			    "mv %2, %0\nbge %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x18:
		AMO_ATTEMPT("lr.w", "sc.w",
			    "mv %2, %0\nbltu %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x1c:
		AMO_ATTEMPT("lr.w", "sc.w",
			    "mv %2, %0\nbgeu %0, %4, 1f\nmv %2, %4\n1:");
		break;
	default:
		break;
	}
	*old_out = old;
	*failed_out = failed;
}

#if __RISCV_XLEN__ == 64
static void amo_attempt_d(unsigned int funct5, unsigned long addr,
			  unsigned long src, unsigned long *old_out,
			  unsigned long *failed_out)
{
	const unsigned long mprv = MSTATUS_MPRV;
	unsigned long old = 0, new = 0, failed = 1;

	switch (funct5) {
	case 0x01:
		AMO_ATTEMPT("lr.d", "sc.d", "mv %2, %4");
		break;
	case 0x00:
		AMO_ATTEMPT("lr.d", "sc.d", "add %2, %0, %4");
		break;
	case 0x04:
		AMO_ATTEMPT("lr.d", "sc.d", "xor %2, %0, %4");
		break;
	case 0x0c:
		AMO_ATTEMPT("lr.d", "sc.d", "and %2, %0, %4");
		break;
	case 0x08:
		AMO_ATTEMPT("lr.d", "sc.d", "or %2, %0, %4");
		break;
	case 0x10:
		AMO_ATTEMPT("lr.d", "sc.d",
			    "mv %2, %0\nblt %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x14:
		AMO_ATTEMPT("lr.d", "sc.d",
			    "mv %2, %0\nbge %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x18:
		AMO_ATTEMPT("lr.d", "sc.d",
			    "mv %2, %0\nbltu %0, %4, 1f\nmv %2, %4\n1:");
		break;
	case 0x1c:
		AMO_ATTEMPT("lr.d", "sc.d",
			    "mv %2, %0\nbgeu %0, %4, 1f\nmv %2, %4\n1:");
		break;
	default:
		break;
	}
	*old_out = old;
	*failed_out = failed;
}

#endif

/* The operations above, by funct5. */
static bool amo_op_known(unsigned int funct5)
{
	switch (funct5) {
	case 0x00:
	case 0x01:
	case 0x04:
	case 0x08:
	case 0x0c:
	case 0x10:
	case 0x14:
	case 0x18:
	case 0x1c:
		return true;
	default:
		return false;
	}
}

static bool emulate_amo(struct trap_regs *regs, unsigned long insn)
{
	unsigned int funct5 = INSN_FUNCT5(insn), f3 = INSN_FUNCT3(insn);
	unsigned long addr = *trap_reg(regs, INSN_RS1(insn));
	unsigned long src = *trap_reg(regs, INSN_RS2(insn));
	unsigned long old = 0, failed = 1, saved = 0;
	struct hart *h = this_hart();
	struct trap_info fault = {};

	/* LR and SC themselves cannot be emulated: nothing else is atomic. */
	if (!amo_op_known(funct5))
		return false;
	if (f3 != 2 && !(f3 == 3 && __RISCV_XLEN__ == 64))
		return false;
	/* LR.W sign-extends: compare 32-bit operands in the same form. */
	if (f3 == 2 && funct5 >= 0x10)
		src = (unsigned long)(long)(int32_t)src;

	do {
		saved = csr_read(mstatus);
		h->trap_taken = 0;
		h->trap_expected = 1;
		/*
		 * The trapping context's MPP (and MPV); MPRV comes and goes
		 * below.
		 */
		csr_write(mstatus, regs->mstatus & ~MSTATUS_MPRV);
		if (f3 == 2)
			amo_attempt_w(funct5, addr, src, &old, &failed);
#if __RISCV_XLEN__ == 64
		else
			amo_attempt_d(funct5, addr, src, &old, &failed);
#endif
		csr_write(mstatus, saved);
		h->trap_expected = 0;

		if (h->trap_taken) {
			/* An AMO is a store as far as faults go. */
			fault = (struct trap_info){
				.cause = h->trap_cause == CAUSE_LOAD_ACCESS ?
						 CAUSE_STORE_ACCESS :
					 h->trap_cause ==
							 CAUSE_LOAD_PAGE_FAULT ?
						 CAUSE_STORE_PAGE_FAULT :
						 h->trap_cause,
				.tval = addr,
			};
			trap_redirect(regs, &fault);
			return true;
		}
	} while (failed);

	if (INSN_RD(insn))
		*trap_reg(regs, INSN_RD(insn)) = old;
	regs->mepc += 4;
	return true;
}

#else

static bool emulate_amo(struct trap_regs *regs, unsigned long insn)
{
	return false;
}

#endif

void trap_illegal_insn(struct trap_regs *regs, const struct trap_info *info)
{
	unsigned long insn = info->tval;
	struct trap_info fault = {};

	pmu_fw_event(SBI_PMU_FW_ILLEGAL_INSN);

	/* mtval may legally be zero: fetch the instruction ourselves. */
	if (!insn && !unpriv_fetch_insn(regs, &insn, &fault)) {
		trap_redirect(regs, &fault);
		return;
	}

	if ((insn & 3) == 3) {
		if (INSN_OPCODE(insn) == OPCODE_SYSTEM &&
		    emulate_csr_read(regs, insn))
			return;
		if (INSN_OPCODE(insn) == OPCODE_AMO && emulate_amo(regs, insn))
			return;
	}
	trap_redirect(regs, info);
}
