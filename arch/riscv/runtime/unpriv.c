// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/unpriv.h>

/*
 * An access runs with mstatus.MPRV set and the trapping context's MPP (and
 * MPV). MPRV is only set around the access instruction itself: while it is
 * set, every load and store of M-mode goes by the trapping context's
 * rules, the compiler's jump tables and spills included. If the access
 * faults, trap_handler() sees trap_expected, records the cause and skips
 * the instruction, which therefore must be exactly 4 bytes long.
 */
#define UNPRIV_INSN(insn, ...)                                       \
	({                                                           \
		__asm__ __volatile__(".option push\n.option norvc\n" \
				     "csrs mstatus, %2\n" insn "\n"  \
				     "csrc mstatus, %2\n"            \
				     ".option pop" __VA_ARGS__);     \
	})

static bool __noinline unpriv_load(const struct trap_regs *regs,
				   unsigned long addr, unsigned int width,
				   bool exec, unsigned long *val,
				   struct trap_info *fault)
{
	struct hart *h = this_hart();
	const unsigned long mprv = MSTATUS_MPRV;
	unsigned long saved = csr_read(mstatus);
	unsigned long mstatus = regs->mstatus & ~MSTATUS_MPRV;
	unsigned long v = 0;

	if (exec)
		mstatus |= MSTATUS_MXR;

	h->trap_taken = 0;
	h->trap_expected = 1;
	csr_write(mstatus, mstatus);
	switch (width) {
	case 1:
		UNPRIV_INSN("lbu %0, 0(%1)",
			    : "=&r"(v)
			    : "r"(addr), "r"(mprv)
			    : "memory");
		break;
	case 2:
		UNPRIV_INSN("lhu %0, 0(%1)",
			    : "=&r"(v)
			    : "r"(addr), "r"(mprv)
			    : "memory");
		break;
#if __RISCV_XLEN__ == 64
	case 4:
		UNPRIV_INSN("lwu %0, 0(%1)",
			    : "=&r"(v)
			    : "r"(addr), "r"(mprv)
			    : "memory");
		break;
	default:
		UNPRIV_INSN("ld %0, 0(%1)",
			    : "=&r"(v)
			    : "r"(addr), "r"(mprv)
			    : "memory");
		break;
#else
	default:
		UNPRIV_INSN("lw %0, 0(%1)",
			    : "=&r"(v)
			    : "r"(addr), "r"(mprv)
			    : "memory");
		break;
#endif
	}
	csr_write(mstatus, saved);
	h->trap_expected = 0;

	if (h->trap_taken) {
		*fault = (struct trap_info){
			.cause = h->trap_cause,
			.tval = h->trap_tval,
			.virt = false,
		};
		return false;
	}
	*val = v;
	return true;
}

bool unpriv_read(const struct trap_regs *regs, unsigned long addr,
		 unsigned int width, unsigned long *val,
		 struct trap_info *fault)
{
	return unpriv_load(regs, addr, width, false, val, fault);
}

bool unpriv_write_byte(const struct trap_regs *regs, unsigned long addr,
		       uint8_t val, struct trap_info *fault)
{
	struct hart *h = this_hart();
	const unsigned long mprv = MSTATUS_MPRV;
	unsigned long saved = csr_read(mstatus), v = val;

	h->trap_taken = 0;
	h->trap_expected = 1;
	csr_write(mstatus, regs->mstatus & ~MSTATUS_MPRV);
	UNPRIV_INSN("sb %0, 0(%1)",
		    :
		    : "r"(v), "r"(addr), "r"(mprv)
		    : "memory");
	csr_write(mstatus, saved);
	h->trap_expected = 0;

	if (h->trap_taken) {
		*fault = (struct trap_info){
			.cause = h->trap_cause,
			.tval = h->trap_tval,
		};
		return false;
	}
	return true;
}

bool unpriv_fetch_insn(const struct trap_regs *regs, unsigned long *insn,
		       struct trap_info *fault)
{
	unsigned long lo = 0, hi = 0;

	if (!unpriv_load(regs, regs->mepc, 2, true, &lo, fault))
		goto fetch_fault;
	if ((lo & 3) != 3) {
		*insn = lo;
		return true;
	}
	if (!unpriv_load(regs, regs->mepc + 2, 2, true, &hi, fault))
		goto fetch_fault;
	*insn = lo | (hi << 16);
	return true;

fetch_fault:
	/* Report the load fault as the matching instruction fetch fault. */
	if (fault->cause == CAUSE_LOAD_ACCESS)
		fault->cause = CAUSE_FETCH_ACCESS;
	else if (fault->cause == CAUSE_LOAD_PAGE_FAULT)
		fault->cause = CAUSE_FETCH_PAGE_FAULT;
	else if (fault->cause == CAUSE_LOAD_GUEST_PAGE_FAULT)
		fault->cause = CAUSE_FETCH_GUEST_PAGE_FAULT;
	return false;
}
