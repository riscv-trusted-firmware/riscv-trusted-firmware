// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Misaligned vector loads and stores (V extension), the vector half of
 * misaligned.c: unit-stride, strided and indexed accesses, masked or not,
 * with segments, whole-register and fault-only-first loads, and register
 * groups of any LMUL.
 *
 * Speed is no concern here, being right is: the access is redone one
 * element at a time from vstart, and an element is moved between its vector
 * register and memory through a bounce buffer, with the register read or
 * written whole by "vse8.v / vle8.v vN, (a0)" under a temporary vtype (the
 * firmware is not built with V, so by encoding). Element order is kept, so
 * a fault leaves vstart at the element that took it, with nothing beyond
 * it touched in memory.
 */

#include <arch/hart.h>
#include <arch/trap.h>
#include <arch/unpriv.h>
#include <util.h>

#define CSR_VSTART 0x008
#define CSR_VL 0xc20
#define CSR_VTYPE 0xc21
#define CSR_VLENB 0xc22

#define MSTATUS_VS GENMASK_UL(10, 9)
/*
 * Registers wider than this are not emulated (the bounce buffer is on the
 * stack).
 */
#define VLENB_MAX 256

#define MOP_UNIT_STRIDE 0
#define MOP_INDEXED_UNORDERED 1
#define MOP_STRIDED 2
#define MOP_INDEXED_ORDERED 3
#define UMOP_UNIT 0x00
#define UMOP_WHOLE_REG 0x08
#define UMOP_FAULT_FIRST 0x10

/* vsetvli t0, a1, e8, m1  /  vsetvl t0, a1, a2  /  vle8.v, vse8.v vN, (a0) */
#define V_SETVLI_E8M1 0x0005f2d7
#define V_SETVL 0x80c5f2d7
#define V_LE8(n) (0x02050007 | ((n) << 7))
#define V_SE8(n) (0x02050027 | ((n) << 7))

/* Register n: its vle8.v and vse8.v encodings, by number. */
#define V_CASE(n, le, se)                                         \
	case n:                                                   \
		if (put)                                          \
			__asm__ __volatile__(".4byte " TO_STR(le) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		else                                              \
			__asm__ __volatile__(".4byte " TO_STR(se) \
					     :                    \
					     : "r"(a0)            \
					     : "memory");         \
		break

/* vl = 'vl' with the vtype as it is: vsetvl t0, a1, a2. */
static void vl_set(unsigned long vl)
{
	register unsigned long a1 __asm__("a1") = vl;
	register unsigned long a2 __asm__("a2") = csr_read(CSR_VTYPE);

	__asm__ __volatile__(".4byte " TO_STR(V_SETVL)
			     :
			     : "r"(a1), "r"(a2)
			     : "t0", "memory");
}

/*
 * The 'bytes' of one element at 'addr', a byte at a time as the trapping
 * context: stored from *val, or loaded into it. false: a fault, in *fault.
 */
static bool elem_access(const struct trap_regs *regs, unsigned long addr,
			unsigned int bytes, bool store, uint64_t *val,
			struct trap_info *fault)
{
	unsigned long byte = 0;

	for (unsigned int b = 0; b < bytes; b++) {
		if (store) {
			if (!unpriv_write_byte(regs, addr + b,
					       (uint8_t)(*val >> (8 * b)),
					       fault))
				return false;
		} else {
			if (!unpriv_read(regs, addr + b, 1, &byte, fault))
				return false;
			*val |= SHIFT_U64(byte, 8 * b);
		}
	}
	return true;
}

/* Move all of vector register 'reg' to (put = false) or from 'buf'. */
static void vreg_move(unsigned int reg, uint8_t *buf, bool put)
{
	register uint8_t *a0 __asm__("a0") = buf;
	register unsigned long a1 __asm__("a1") = csr_read(CSR_VLENB);
	register unsigned long a2 __asm__("a2");
	unsigned long vl = csr_read(CSR_VL), vtype = csr_read(CSR_VTYPE);
	unsigned long vstart = csr_read(CSR_VSTART);

	/* vl = VLENB elements of 8 bits: the whole register, no more. */
	__asm__ __volatile__(".4byte " TO_STR(V_SETVLI_E8M1)
			     :
			     : "r"(a1)
			     : "t0", "memory");
	switch (reg) {
		V_CASE(0, V_LE8(0), V_SE8(0));
		V_CASE(1, V_LE8(1), V_SE8(1));
		V_CASE(2, V_LE8(2), V_SE8(2));
		V_CASE(3, V_LE8(3), V_SE8(3));
		V_CASE(4, V_LE8(4), V_SE8(4));
		V_CASE(5, V_LE8(5), V_SE8(5));
		V_CASE(6, V_LE8(6), V_SE8(6));
		V_CASE(7, V_LE8(7), V_SE8(7));
		V_CASE(8, V_LE8(8), V_SE8(8));
		V_CASE(9, V_LE8(9), V_SE8(9));
		V_CASE(10, V_LE8(10), V_SE8(10));
		V_CASE(11, V_LE8(11), V_SE8(11));
		V_CASE(12, V_LE8(12), V_SE8(12));
		V_CASE(13, V_LE8(13), V_SE8(13));
		V_CASE(14, V_LE8(14), V_SE8(14));
		V_CASE(15, V_LE8(15), V_SE8(15));
		V_CASE(16, V_LE8(16), V_SE8(16));
		V_CASE(17, V_LE8(17), V_SE8(17));
		V_CASE(18, V_LE8(18), V_SE8(18));
		V_CASE(19, V_LE8(19), V_SE8(19));
		V_CASE(20, V_LE8(20), V_SE8(20));
		V_CASE(21, V_LE8(21), V_SE8(21));
		V_CASE(22, V_LE8(22), V_SE8(22));
		V_CASE(23, V_LE8(23), V_SE8(23));
		V_CASE(24, V_LE8(24), V_SE8(24));
		V_CASE(25, V_LE8(25), V_SE8(25));
		V_CASE(26, V_LE8(26), V_SE8(26));
		V_CASE(27, V_LE8(27), V_SE8(27));
		V_CASE(28, V_LE8(28), V_SE8(28));
		V_CASE(29, V_LE8(29), V_SE8(29));
		V_CASE(30, V_LE8(30), V_SE8(30));
		V_CASE(31, V_LE8(31), V_SE8(31));
	default:
		break;
	}
	/* Back to the interrupted vl and vtype; vsetvl zeroes vstart. */
	a1 = vl;
	a2 = vtype;
	__asm__ __volatile__(".4byte " TO_STR(V_SETVL)
			     :
			     : "r"(a1), "r"(a2)
			     : "t0", "memory");
	csr_write(CSR_VSTART, vstart);
}

/*
 * 'len' bytes at byte offset 'off' of the register group that starts at 'reg'.
 */
static void vgroup_access(unsigned int reg, unsigned long off, unsigned int len,
			  uint64_t *val, bool put)
{
	unsigned long vlenb = csr_read(CSR_VLENB);
	uint8_t buf[VLENB_MAX] = {};

	reg = (reg + off / vlenb) & 31;
	off %= vlenb;
	vreg_move(reg, buf, false);
	for (unsigned int i = 0; i < len; i++) {
		if (put)
			buf[off + i] = (uint8_t)(*val >> (8 * i));
		else
			*val |= SHIFT_U64(buf[off + i], 8 * i);
	}
	if (put)
		vreg_move(reg, buf, true);
}

/* true: handled, one way or the other. false: not something we emulate. */
bool trap_misaligned_vector(struct trap_regs *regs, unsigned long insn)
{
	unsigned int f3 = (insn >> 12) & 7, mop = (insn >> 26) & 3;
	unsigned int umop = (insn >> 20) & 0x1f, nf = ((insn >> 29) & 7) + 1;
	unsigned int vreg = (insn >> 7) & 0x1f, eew = 0, data_bytes = 0;
	bool store = (insn & 0x7f) == 0x27, masked = !(insn & BIT(25));
	bool indexed = mop == MOP_INDEXED_UNORDERED ||
		       mop == MOP_INDEXED_ORDERED;
	bool whole = false, fault_first = false;
	unsigned long base = *trap_reg(regs, (insn >> 15) & 0x1f);
	unsigned long vlenb = 0, vl = 0, first = 0, lmul_bytes = 0;
	struct trap_info fault = {};

	/*
	 * Widths 8, 16, 32, 64 (funct3 0, 5, 6, 7); the others are scalar FP.
	 */
	if ((f3 != 0 && f3 < 5) || (insn & BIT(28)) ||
	    !(csr_read(misa) & MISA_EXT('V')) || !(regs->mstatus & MSTATUS_VS))
		return false;
	eew = f3 ? BIT32(f3 - 4) : 1;

	vlenb = csr_read(CSR_VLENB);
	if (vlenb > VLENB_MAX)
		return false;
	if (mop == MOP_UNIT_STRIDE) {
		whole = umop == UMOP_WHOLE_REG;
		fault_first = umop == UMOP_FAULT_FIRST && !store;
		if (umop != UMOP_UNIT && !whole && !fault_first)
			/* mask load / store: bytes, never misaligned */
			return false;
	}

	/*
	 * Indexed: the instruction gives the index width, vtype the data width.
	 */
	data_bytes = indexed ? BIT32((csr_read(CSR_VTYPE) >> 3) & 7) : eew;
	if (data_bytes > 8)
		return false;
	/* Bytes of one field's register group: LMUL registers, at least one. */
	lmul_bytes = vlenb;
	if (!whole && (csr_read(CSR_VTYPE) & 7) < 4)
		lmul_bytes <<= csr_read(CSR_VTYPE) & 7;

	vl = whole ? vlenb / data_bytes : csr_read(CSR_VL);
	first = whole ? 0 : csr_read(CSR_VSTART);

	for (unsigned long i = first; i < vl; i++) {
		uint64_t mask = 0, index = 0;
		unsigned long addr = 0;

		if (masked) {
			vgroup_access(0, i / 8, 1, &mask, false);
			if (!((mask >> (i % 8)) & 1))
				continue;
		}
		if (indexed) {
			vgroup_access((insn >> 20) & 0x1f, i * eew, eew, &index,
				      false);
			addr = base + (unsigned long)index;
		} else if (mop == MOP_STRIDED) {
			addr = base + i * *trap_reg(regs, (insn >> 20) & 0x1f);
		} else {
			addr = base + i * nf * data_bytes;
		}

		for (unsigned int f = 0; f < nf; f++, addr += data_bytes) {
			unsigned long off = f * lmul_bytes + i * data_bytes;
			uint64_t val = 0;
			bool ok = false;

			if (store)
				vgroup_access(vreg, off, data_bytes, &val,
					      false);
			ok = elem_access(regs, addr, data_bytes, store, &val,
					 &fault);
			if (!ok) {
				/*
				 * Fault-only-first past element 0: a shorter
				 * vl, no trap.
				 */
				if (fault_first && i) {
					vl_set(i);
					goto done;
				}
				csr_write(CSR_VSTART, i);
				regs->mstatus |= MSTATUS_VS;
				trap_redirect(regs, &fault);
				return true;
			}
			if (!store)
				vgroup_access(vreg, off, data_bytes, &val,
					      true);
		}
	}
done:
	csr_write(CSR_VSTART, 0);
	/*
	 * The register file is restored from 'regs': the vector state is dirty
	 * now.
	 */
	regs->mstatus |= MSTATUS_VS;
	regs->mepc += 4;
	return true;
}
