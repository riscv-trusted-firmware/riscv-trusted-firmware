// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/fwft.h>
#include <arch/hart.h>
#include <domain.h>
#include <sbi/sbi.h>
#include <util.h>

#define FWFT_FEATURES SBI_FWFT_LOCAL_RESERVED_START

/* Per domain and hart, see <domain.h>. */
static struct fwft_hart {
	/* Bit per feature: locked until the hart is started again. */
	unsigned long locked;
	/* While the hart runs another domain: what the features were set to. */
	uint64_t envcfg;
	bool misaligned_deleg;
} *fwft_harts;

void fwft_init(void)
{
	fwft_harts = domain_hart_alloc(sizeof(*fwft_harts));
}

static struct fwft_hart *this_fwft(void)
{
	return this_domain_hart_slot(fwft_harts, sizeof(*fwft_harts));
}

#define MISALIGNED_DELEG \
	(BIT(CAUSE_MISALIGNED_LOAD) | BIT(CAUSE_MISALIGNED_STORE))

static uint64_t envcfg_read(void)
{
	uint64_t val = csr_read(CSR_MENVCFG);

#if __RISCV_XLEN__ == 32
	val |= SHIFT_U64(csr_read(CSR_MENVCFGH), 32);
#endif
	return val;
}

static void envcfg_write(uint64_t val)
{
	csr_write(CSR_MENVCFG, (unsigned long)val);
#if __RISCV_XLEN__ == 32
	csr_write(CSR_MENVCFGH, (unsigned long)(val >> 32));
#endif
}

/* Write 'val' into the field 'mask' of menvcfg; false when it did not stick. */
static bool envcfg_update(uint64_t mask, uint64_t val)
{
	uint64_t old = 0, new = 0;

	if (!hart_has(HART_FEAT_MENVCFG))
		return false;
	old = envcfg_read();
	new = (old & ~mask) | val;
	envcfg_write(new);
	if ((envcfg_read() & mask) == val)
		return true;
	envcfg_write(old);
	return false;
}

/* menvcfg field of a feature, 0 for the features that are not in menvcfg. */
static uint64_t feature_field(unsigned long feature)
{
	switch (feature) {
	case SBI_FWFT_LANDING_PAD:
		return BIT64(ENVCFG_LPE_BIT);
	case SBI_FWFT_SHADOW_STACK:
		return BIT64(ENVCFG_SSE_BIT);
	case SBI_FWFT_DOUBLE_TRAP:
		return BIT64(ENVCFG_DTE_BIT);
	case SBI_FWFT_PTE_AD_HW_UPDATING:
		return BIT64(ENVCFG_ADUE_BIT);
	case SBI_FWFT_POINTER_MASKING_PMLEN:
		return SHIFT_U64(3, ENVCFG_PMM_SHIFT);
	default:
		return 0;
	}
}

/* A WARL field that cannot be set belongs to an extension the hart lacks. */
static bool feature_exists(unsigned long feature)
{
	uint64_t field = feature_field(feature), old = 0;
	bool ok = false;

	if (feature == SBI_FWFT_MISALIGNED_EXC_DELEG)
		return true;
	if (!field || !hart_has(HART_FEAT_MENVCFG))
		return false;
	old = envcfg_read();
	ok = envcfg_update(field, field);
	envcfg_write(old);
	return ok;
}

static long feature_check(unsigned long feature)
{
	if (feature < FWFT_FEATURES)
		return feature_exists(feature) ? SBI_SUCCESS :
						 SBI_ERR_NOT_SUPPORTED;
	/* Reserved ranges are denied, the platform ones are empty. */
	if (feature < SBI_FWFT_LOCAL_PLATFORM_START ||
	    (feature >= SBI_FWFT_GLOBAL_RESERVED_START &&
	     feature < SBI_FWFT_GLOBAL_PLATFORM_START))
		return SBI_ERR_DENIED;
	return SBI_ERR_NOT_SUPPORTED;
}

void fwft_hart_init(void)
{
	this_fwft()->locked = 0;
	/*
	 * hart_runtime_init() has put medeleg and menvcfg at their reset
	 * values.
	 */
}

/* Every menvcfg field a feature stands for. */
static uint64_t feature_fields(void)
{
	uint64_t fields = 0;

	for (unsigned long f = 0; f < FWFT_FEATURES; f++)
		fields |= feature_field(f);
	return fields;
}

void fwft_hart_switch_out(void)
{
	struct fwft_hart *f = this_fwft();

	f->misaligned_deleg = csr_read(medeleg) & MISALIGNED_DELEG;
	if (hart_has(HART_FEAT_MENVCFG))
		f->envcfg = envcfg_read() & feature_fields();
}

void fwft_hart_switch_in(bool fresh)
{
	struct fwft_hart *f = this_fwft();

	/* Every feature is off out of reset. */
	if (fresh)
		*f = (struct fwft_hart){ 0 };
	if (f->misaligned_deleg)
		csr_set(medeleg, MISALIGNED_DELEG);
	else
		csr_clear(medeleg, MISALIGNED_DELEG);
	if (hart_has(HART_FEAT_MENVCFG))
		envcfg_write((envcfg_read() & ~feature_fields()) | f->envcfg);
}

long fwft_get(unsigned long feature, unsigned long *value)
{
	long rc = feature_check(feature);
	uint64_t field = 0;

	if (rc)
		return rc;
	if (feature == SBI_FWFT_MISALIGNED_EXC_DELEG) {
		*value = (csr_read(medeleg) & MISALIGNED_DELEG) ==
			 MISALIGNED_DELEG;
		return SBI_SUCCESS;
	}

	field = envcfg_read() & feature_field(feature);
	if (feature == SBI_FWFT_POINTER_MASKING_PMLEN) {
		field >>= ENVCFG_PMM_SHIFT;
		*value = field == 2 ? 7 : field == 3 ? 16 : 0;
	} else {
		*value = field != 0;
	}
	return SBI_SUCCESS;
}

long fwft_set(unsigned long feature, unsigned long value, unsigned long flags)
{
	unsigned long *lock = &this_fwft()->locked;
	long rc = feature_check(feature);
	uint64_t field = 0, bits = 0;

	if (rc)
		return rc;
	if (flags & ~SBI_FWFT_SET_FLAG_LOCK)
		return SBI_ERR_INVALID_PARAM;
	if (*lock & BIT(feature))
		return SBI_ERR_DENIED_LOCKED;

	if (feature == SBI_FWFT_MISALIGNED_EXC_DELEG) {
		if (value > 1)
			return SBI_ERR_INVALID_PARAM;
		if (value)
			csr_set(medeleg, MISALIGNED_DELEG);
		else
			csr_clear(medeleg, MISALIGNED_DELEG);
	} else {
		field = feature_field(feature);
		if (feature == SBI_FWFT_POINTER_MASKING_PMLEN) {
			if (value != 0 && value != 7 && value != 16)
				return SBI_ERR_INVALID_PARAM;
			bits = (uint64_t)(value == 7  ? 2 :
					  value == 16 ? 3 :
							0)
			       << ENVCFG_PMM_SHIFT;
		} else {
			if (value > 1)
				return SBI_ERR_INVALID_PARAM;
			bits = value ? field : 0;
		}
		/* A PMLEN the hart does not implement does not stick. */
		if (!envcfg_update(field, bits))
			return SBI_ERR_INVALID_PARAM;
	}

	if (flags & SBI_FWFT_SET_FLAG_LOCK)
		*lock |= BIT(feature);
	return SBI_SUCCESS;
}
