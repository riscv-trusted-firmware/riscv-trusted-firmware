// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Physical memory protection. pmpcfg/pmpaddr CSRs are selected by number,
 * hence the switch over the entry index.
 */

#include <arch/csr.h>
#include <arch/pmp.h>
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

int pmp_set_napot(unsigned int idx, unsigned long base, unsigned long size,
		  unsigned int perm)
{
	/* NAPOT: power of two, at least 8 bytes, base aligned to the size. */
	if (idx >= 16 || size < 8 || !IS_POWER_OF_TWO(size) ||
	    !IS_ALIGNED(base, size))
		return -1;

	pmpaddr_write(idx, (base >> 2) | ((size >> 3) - 1));
	pmpcfg_update(idx, PMP_A_NAPOT | perm);
	return 0;
}

void pmp_set_all(unsigned int idx, unsigned int perm)
{
	if (idx >= 16)
		return;
	pmpaddr_write(idx, ~UL(0));
	pmpcfg_update(idx, PMP_A_NAPOT | perm);
}
