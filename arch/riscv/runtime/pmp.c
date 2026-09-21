// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Physical memory protection: the PMP CSRs. pmpcfg/pmpaddr are selected by
 * number, hence the switch over the entry index. What goes into the entries
 * is decided in hart.c.
 */

#include <arch/csr.h>
#include <arch/pmp.h>
#include <types_ext.h>
#include <util.h>

#define PMP_CFG_PER_REG (__RISCV_XLEN__ / 8)

static void pmpaddr_write(unsigned int idx, unsigned long val)
{
	switch (idx) {
	case 0:
		csr_write(CSR_PMPADDR0, val);
		break;
	case 1:
		csr_write(CSR_PMPADDR0 + 1, val);
		break;
	case 2:
		csr_write(CSR_PMPADDR0 + 2, val);
		break;
	case 3:
		csr_write(CSR_PMPADDR0 + 3, val);
		break;
	case 4:
		csr_write(CSR_PMPADDR0 + 4, val);
		break;
	case 5:
		csr_write(CSR_PMPADDR0 + 5, val);
		break;
	case 6:
		csr_write(CSR_PMPADDR0 + 6, val);
		break;
	case 7:
		csr_write(CSR_PMPADDR0 + 7, val);
		break;
	case 8:
		csr_write(CSR_PMPADDR0 + 8, val);
		break;
	case 9:
		csr_write(CSR_PMPADDR0 + 9, val);
		break;
	case 10:
		csr_write(CSR_PMPADDR0 + 10, val);
		break;
	case 11:
		csr_write(CSR_PMPADDR0 + 11, val);
		break;
	case 12:
		csr_write(CSR_PMPADDR0 + 12, val);
		break;
	case 13:
		csr_write(CSR_PMPADDR0 + 13, val);
		break;
	case 14:
		csr_write(CSR_PMPADDR0 + 14, val);
		break;
	case 15:
		csr_write(CSR_PMPADDR0 + 15, val);
		break;
	default:
		break;
	}
}

static void pmpcfg_update(unsigned int idx, unsigned int cfg)
{
	unsigned int shift = 8 * (idx % PMP_CFG_PER_REG);
	unsigned long mask = SHIFT_UL(0xff, shift);
	unsigned long val = SHIFT_UL(cfg, shift);

	/* RV64 only has the even-numbered pmpcfg registers. */
	switch (idx / PMP_CFG_PER_REG * (PMP_CFG_PER_REG / 4)) {
	case 0:
		csr_clear(CSR_PMPCFG0, mask);
		csr_set(CSR_PMPCFG0, val);
		break;
	case 1:
		csr_clear(CSR_PMPCFG0 + 1, mask);
		csr_set(CSR_PMPCFG0 + 1, val);
		break;
	case 2:
		csr_clear(CSR_PMPCFG0 + 2, mask);
		csr_set(CSR_PMPCFG0 + 2, val);
		break;
	case 3:
		csr_clear(CSR_PMPCFG0 + 3, mask);
		csr_set(CSR_PMPCFG0 + 3, val);
		break;
	default:
		break;
	}
}

void pmp_entry_cfg(unsigned int idx, unsigned int cfg)
{
	if (idx < 16)
		pmpcfg_update(idx, cfg);
}

void pmp_entry_set(unsigned int idx, unsigned long pmpaddr, unsigned int cfg)
{
	if (idx >= 16)
		return;
	/* Off while the address changes. A locked entry ignores all of this. */
	pmpcfg_update(idx, 0);
	pmpaddr_write(idx, pmpaddr);
	pmpcfg_update(idx, cfg);
}

/*
 * The grain: pmpaddr bits below it read as zero (as ones, in a NAPOT
 * entry), so a NAPOT region smaller than it becomes one grain, which errs
 * on the side of the rule, and the top of a TOR range is rounded down,
 * which does not: the tail of the range would be left out.
 */
static unsigned long grain = 4;

void pmp_grain_set(unsigned long pmpaddr_ones)
{
	for (grain = 4; pmpaddr_ones && !(pmpaddr_ones & 1); pmpaddr_ones >>= 1)
		grain <<= 1;
}

unsigned long pmp_grain(void)
{
	return grain;
}

unsigned int pmp_range_set(unsigned int idx, paddr_t base, paddr_size_t size,
			   unsigned int perm)
{
	/* NAPOT: power of two, at least 8 bytes, base aligned to the size. */
	if (size >= 8 && IS_POWER_OF_TWO(size) && IS_ALIGNED(base, size)) {
		pmp_entry_set(idx, (base >> 2) | ((size >> 3) - 1),
			      PMP_A_NAPOT | perm);
		return 1;
	}
	/* TOR: from the previous entry's address up to this one's. */
	pmp_entry_set(idx, base >> 2, 0);
	pmp_entry_set(idx + 1, ROUNDUP2(base + size, grain) >> 2,
		      PMP_A_TOR | perm);
	return 2;
}

void pmp_all_set(unsigned int idx, unsigned int perm)
{
	pmp_entry_set(idx, ~UL(0), PMP_A_NAPOT | perm);
}
