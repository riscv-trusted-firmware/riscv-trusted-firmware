// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Illegal instruction handling: emulate what the hart is allowed not to
 * implement, hand everything else to S-mode.
 *
 * Emulated today: reads of the 'time' (and 'timeh') CSR on harts without
 * it, backed by the platform timer.
 */

#include <arch/hart.h>
#include <arch/trap.h>
#include <arch/unpriv.h>
#include <timer.h>

#define INSN_OPCODE(i) ((i) & 0x7f)
#define INSN_RD(i) (((i) >> 7) & 0x1f)
#define INSN_FUNCT3(i) (((i) >> 12) & 7)
#define INSN_RS1(i) (((i) >> 15) & 0x1f)
#define INSN_CSR(i) (((i) >> 20) & 0xfff)

#define OPCODE_SYSTEM 0x73
#define FUNCT3_CSRRS 2
#define FUNCT3_CSRRC 3
#define FUNCT3_CSRRSI 6
#define FUNCT3_CSRRCI 7

static bool emulate_csr_read(struct trap_regs *regs, unsigned long insn)
{
	unsigned int f3 = INSN_FUNCT3(insn);
	uint64_t now = 0;

	/* Read-only CSRs: only csrrs/csrrc[i] with a zero source are reads. */
	if (f3 != FUNCT3_CSRRS && f3 != FUNCT3_CSRRC && f3 != FUNCT3_CSRRSI &&
	    f3 != FUNCT3_CSRRCI)
		return false;
	if (INSN_RS1(insn) != 0 || !timer_available())
		return false;

	now = timer_now();
	switch (INSN_CSR(insn)) {
	case CSR_TIME:
		break;
#if __RISCV_XLEN__ == 32
	case CSR_TIMEH:
		now >>= 32;
		break;
#endif
	default:
		return false;
	}

	if (INSN_RD(insn))
		*trap_reg(regs, INSN_RD(insn)) = (unsigned long)now;
	regs->mepc += 4;
	return true;
}

void trap_illegal_insn(struct trap_regs *regs, const struct trap_info *info)
{
	unsigned long insn = info->tval;
	struct trap_info fault = {};

	/* mtval may legally be zero: fetch the instruction ourselves. */
	if (!insn && !unpriv_fetch_insn(regs, &insn, &fault)) {
		trap_redirect(regs, &fault);
		return;
	}

	if ((insn & 3) == 3 && INSN_OPCODE(insn) == OPCODE_SYSTEM &&
	    emulate_csr_read(regs, insn))
		return;

	trap_redirect(regs, info);
}
