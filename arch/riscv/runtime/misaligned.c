// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Misaligned load and store emulation, for harts that trap on them and
 * supervisor software that has not asked to handle them itself (FWFT
 * MISALIGNED_EXC_DELEG). The access is redone byte by byte with the
 * privilege and translation of the trapping context; a fault on the way
 * goes to S-mode as the fault it is.
 *
 * Covered: the integer and floating-point loads and stores of the base ISA,
 * of the C extension and of Zcb. Not covered (redirected to S-mode): vector
 * accesses, and LR/SC, which cannot be split.
 */

#include <arch/hart.h>
#include <arch/trap.h>
#include <arch/unpriv.h>
#include <util.h>

struct access {
	bool store, fp, sign_extend;
	unsigned int len; /* bytes */
	unsigned int reg; /* rd of a load, rs2 of a store */
	unsigned int insn_len;
};

static bool decode32(unsigned long insn, struct access *a)
{
	unsigned int f3 = (insn >> 12) & 7;

	a->insn_len = 4;
	switch (insn & 0x7f) {
	case 0x03: /* LOAD: lh lw ld lhu lwu */
		a->len = BIT32(f3 & 3);
		a->sign_extend = !(f3 & 4);
		a->reg = (insn >> 7) & 0x1f;
		return true;
	case 0x23: /* STORE: sh sw sd */
		a->store = true;
		a->len = BIT32(f3 & 3);
		a->reg = (insn >> 20) & 0x1f;
		return f3 < 4;
	case 0x07: /* LOAD-FP: flw fld */
	case 0x27: /* STORE-FP: fsw fsd */
		a->fp = true;
		a->store = (insn & 0x7f) == 0x27;
		a->len = BIT32(f3);
		a->reg = (insn >> (a->store ? 20 : 7)) & 0x1f;
		return f3 == 2 || f3 == 3;
	default:
		return false;
	}
}

static bool decode16(unsigned long insn, struct access *a)
{
	unsigned int f3 = (insn >> 13) & 7, quadrant = insn & 3;
	bool sp_relative = quadrant == 2;

	if (quadrant != 0 && quadrant != 2)
		return false;
	a->insn_len = 2;
	a->sign_extend = true;

	switch (f3) {
	case 2: /* c.lw, c.lwsp */
	case 6: /* c.sw, c.swsp */
		a->len = 4;
		break;
	case 3: /* RV64: c.ld, c.ldsp; RV32: c.flw, c.flwsp */
	case 7: /* RV64: c.sd, c.sdsp; RV32: c.fsw, c.fswsp */
		a->len = __RISCV_XLEN__ / 8;
		a->fp = __RISCV_XLEN__ == 32;
		break;
	case 1: /* c.fld, c.fldsp */
	case 5: /* c.fsd, c.fsdsp */
		a->len = 8;
		a->fp = true;
		break;
	case 4:
		/*
		 * Zcb, quadrant 0: c.lhu / c.lh (100001, bit 6), c.sh (100011).
		 */
		if (sp_relative ||
		    (((insn >> 10) & 7) != 1 && ((insn >> 10) & 7) != 3))
			return false;
		a->len = 2;
		a->store = ((insn >> 10) & 7) == 3;
		a->sign_extend = insn & BIT(6);
		a->reg = 8 + ((insn >> 2) & 7);
		return true;
	default:
		return false;
	}
	a->store = f3 >= 5;

	if (!sp_relative)
		a->reg = 8 + ((insn >> 2) & 7);
	else if (a->store)
		a->reg = (insn >> 2) & 0x1f;
	else
		a->reg = (insn >> 7) & 0x1f;
	/* c.lwsp and c.ldsp with rd = x0 are reserved encodings. */
	return a->fp || a->store || !sp_relative || a->reg;
}

/*
 * Floating-point registers go through memory: "fld/fsd fN, 0(a0)" by
 * encoding, since the firmware is not built with F or D. The width of the
 * memory access is the width of the emulated one, so a single-precision
 * load NaN-boxes as the hardware does.
 */
#define FPL(n, f3) (0x00000007 | ((f3) << 12) | (10 << 15) | ((n) << 7))
#define FPS(n, f3) (0x00000027 | ((f3) << 12) | (10 << 15) | ((n) << 20))
/* Register n: its fsd, fsw, fld and flw encodings, by number. */
#define FP_CASE(n, sd, sw, ld, lw)                                \
	case n:                                                   \
		if (store && dbl)                                 \
			__asm__ __volatile__(".4byte " TO_STR(sd) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		else if (store)                                   \
			__asm__ __volatile__(".4byte " TO_STR(sw) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		else if (dbl)                                     \
			__asm__ __volatile__(".4byte " TO_STR(ld) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		else                                              \
			__asm__ __volatile__(".4byte " TO_STR(lw) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		break

/* store: fN -> *buf; else *buf -> fN. */
static void fp_reg_access(unsigned int reg, bool store, bool dbl, uint64_t *buf)
{
	register uint64_t *a0 __asm__("a0") = buf;

	switch (reg) {
		FP_CASE(0, FPS(0, 3), FPS(0, 2), FPL(0, 3), FPL(0, 2));
		FP_CASE(1, FPS(1, 3), FPS(1, 2), FPL(1, 3), FPL(1, 2));
		FP_CASE(2, FPS(2, 3), FPS(2, 2), FPL(2, 3), FPL(2, 2));
		FP_CASE(3, FPS(3, 3), FPS(3, 2), FPL(3, 3), FPL(3, 2));
		FP_CASE(4, FPS(4, 3), FPS(4, 2), FPL(4, 3), FPL(4, 2));
		FP_CASE(5, FPS(5, 3), FPS(5, 2), FPL(5, 3), FPL(5, 2));
		FP_CASE(6, FPS(6, 3), FPS(6, 2), FPL(6, 3), FPL(6, 2));
		FP_CASE(7, FPS(7, 3), FPS(7, 2), FPL(7, 3), FPL(7, 2));
		FP_CASE(8, FPS(8, 3), FPS(8, 2), FPL(8, 3), FPL(8, 2));
		FP_CASE(9, FPS(9, 3), FPS(9, 2), FPL(9, 3), FPL(9, 2));
		FP_CASE(10, FPS(10, 3), FPS(10, 2), FPL(10, 3), FPL(10, 2));
		FP_CASE(11, FPS(11, 3), FPS(11, 2), FPL(11, 3), FPL(11, 2));
		FP_CASE(12, FPS(12, 3), FPS(12, 2), FPL(12, 3), FPL(12, 2));
		FP_CASE(13, FPS(13, 3), FPS(13, 2), FPL(13, 3), FPL(13, 2));
		FP_CASE(14, FPS(14, 3), FPS(14, 2), FPL(14, 3), FPL(14, 2));
		FP_CASE(15, FPS(15, 3), FPS(15, 2), FPL(15, 3), FPL(15, 2));
		FP_CASE(16, FPS(16, 3), FPS(16, 2), FPL(16, 3), FPL(16, 2));
		FP_CASE(17, FPS(17, 3), FPS(17, 2), FPL(17, 3), FPL(17, 2));
		FP_CASE(18, FPS(18, 3), FPS(18, 2), FPL(18, 3), FPL(18, 2));
		FP_CASE(19, FPS(19, 3), FPS(19, 2), FPL(19, 3), FPL(19, 2));
		FP_CASE(20, FPS(20, 3), FPS(20, 2), FPL(20, 3), FPL(20, 2));
		FP_CASE(21, FPS(21, 3), FPS(21, 2), FPL(21, 3), FPL(21, 2));
		FP_CASE(22, FPS(22, 3), FPS(22, 2), FPL(22, 3), FPL(22, 2));
		FP_CASE(23, FPS(23, 3), FPS(23, 2), FPL(23, 3), FPL(23, 2));
		FP_CASE(24, FPS(24, 3), FPS(24, 2), FPL(24, 3), FPL(24, 2));
		FP_CASE(25, FPS(25, 3), FPS(25, 2), FPL(25, 3), FPL(25, 2));
		FP_CASE(26, FPS(26, 3), FPS(26, 2), FPL(26, 3), FPL(26, 2));
		FP_CASE(27, FPS(27, 3), FPS(27, 2), FPL(27, 3), FPL(27, 2));
		FP_CASE(28, FPS(28, 3), FPS(28, 2), FPL(28, 3), FPL(28, 2));
		FP_CASE(29, FPS(29, 3), FPS(29, 2), FPL(29, 3), FPL(29, 2));
		FP_CASE(30, FPS(30, 3), FPS(30, 2), FPL(30, 3), FPL(30, 2));
		FP_CASE(31, FPS(31, 3), FPS(31, 2), FPL(31, 3), FPL(31, 2));
	default:
		break;
	}
}

void trap_misaligned(struct trap_regs *regs, const struct trap_info *info)
{
	struct access a = { 0 };
	struct trap_info fault = {};
	unsigned long insn = 0, byte = 0;
	uint64_t val = 0;
	bool ok = false, is_store = false;

	if (!unpriv_fetch_insn(regs, &insn, &fault)) {
		trap_redirect(regs, &fault);
		return;
	}
	ok = (insn & 3) == 3 ? decode32(insn, &a) : decode16(insn, &a);
	/*
	 * Not an access we know, or not the kind that trapped: S-mode's
	 * problem. FP accesses need F/D and live FP state, else the
	 * instruction would have been illegal in the first place.
	 */
	is_store = info->cause == CAUSE_MISALIGNED_STORE;
	if (!ok || a.len < 2 || a.len > 8 || a.store != is_store ||
	    (a.fp && (!(csr_read(misa) & MISA_EXT('F')) ||
		      !(regs->mstatus & MSTATUS_FS) ||
		      (a.len == 8 && !(csr_read(misa) & MISA_EXT('D')))))) {
		trap_redirect(regs, info);
		return;
	}

	if (a.store) {
		if (a.fp)
			fp_reg_access(a.reg, true, a.len == 8, &val);
		else
			val = *trap_reg(regs, a.reg);
		for (unsigned int i = 0; i < a.len; i++)
			if (!unpriv_write_byte(regs, info->tval + i,
					       (uint8_t)(val >> (8 * i)),
					       &fault)) {
				trap_redirect(regs, &fault);
				return;
			}
	} else {
		for (unsigned int i = 0; i < a.len; i++) {
			if (!unpriv_read(regs, info->tval + i, 1, &byte,
					 &fault)) {
				trap_redirect(regs, &fault);
				return;
			}
			val |= SHIFT_U64(byte, 8 * i);
		}
		if (a.fp) {
			fp_reg_access(a.reg, false, a.len == 8, &val);
			/*
			 * The register file is restored from 'regs': FS is
			 * dirty now.
			 */
			regs->mstatus |= MSTATUS_FS;
		} else if (a.reg) {
			if (a.sign_extend && a.len < 8)
				val = (uint64_t)((int64_t)(val
							   << (64 -
							       8 * a.len)) >>
						 (64 - 8 * a.len));
			*trap_reg(regs, a.reg) = (unsigned long)val;
		}
	}
	regs->mepc += a.insn_len;
}
