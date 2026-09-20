// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Debug triggers, see <arch/dbtr.h>.
 *
 * A trigger fires as a breakpoint exception, which is delegated: S-mode
 * takes it directly. The monitor only programs the triggers, and makes
 * sure none of them can fire in M-mode or belongs to a debugger (the m and
 * dmode bits of a configuration must be zero).
 */

#include <arch/dbtr.h>
#include <arch/hart.h>
#include <arch/pmp.h>
#include <sbi/sbi.h>
#include <util.h>

#define TDATA1_TYPE_SHIFT (__RISCV_XLEN__ - 4)
#define TDATA1_TYPE(v) ((v) >> TDATA1_TYPE_SHIFT)
#define TDATA1_DMODE BIT(__RISCV_XLEN__ - 5)
#define TYPE_MCONTROL 2
#define TYPE_MCONTROL6 6

/* mcontrol and mcontrol6 agree on these; vs and vu only exist in mcontrol6. */
#define MC_U BIT(3)
#define MC_S BIT(4)
#define MC_M BIT(6)
#define MC_CHAIN BIT(11)
#define MC6_VU BIT(23)
#define MC6_VS BIT(24)
/* Set by the hardware when the trigger fires: not part of a configuration. */
#define MC_HIT BIT(20)
#define MC6_HIT0 BIT(22)
#define MC6_HIT1 BIT(25)

#define STATE_MAPPED BIT(0)
#define STATE_U BIT(1)
#define STATE_S BIT(2)
#define STATE_VU BIT(3)
#define STATE_VS BIT(4)
#define STATE_HAVE_HW BIT(5)
#define STATE_HW_IDX_SHIFT 8

#define MAX_TRIGGERS CONFIG_SBI_DBTR_MAX_TRIGGERS
#define SHMEM_NONE (~UL(0))

/* One entry of the shared memory: state or index, then the three tdata. */
struct dbtr_entry {
	unsigned long idx, tdata1, tdata2, tdata3;
};

struct dbtr_hart {
	unsigned long shmem;
	unsigned int count;
	bool has_tdata3;
	/* tinfo: bit per supported type */
	uint32_t types[MAX_TRIGGERS];
	unsigned long state[MAX_TRIGGERS];
};

static struct dbtr_hart dbtr_harts[CONFIG_PLATFORM_HART_COUNT];

static struct dbtr_hart *this_dbtr(void)
{
	return &dbtr_harts[this_hart_index()];
}

static unsigned long mode_bits(unsigned long tdata1)
{
	return MC_U | MC_S |
	       (TDATA1_TYPE(tdata1) == TYPE_MCONTROL6 ? MC6_VU | MC6_VS : 0);
}

static unsigned long hit_bits(unsigned long tdata1)
{
	return TDATA1_TYPE(tdata1) == TYPE_MCONTROL6 ? MC6_HIT0 | MC6_HIT1 :
						       MC_HIT;
}

/*
 * A trigger is off when it matches in no mode. Zero says so, but tdata1 is
 * WARL and an implementation may refuse type 0 (QEMU does): what is sure
 * to stick is the current type with everything else clear.
 */
static void hw_clear(unsigned int idx)
{
	unsigned long tdata1 = 0;

	csr_write(CSR_TSELECT, idx);
	csr_write(CSR_TDATA1, 0);
	tdata1 = csr_read(CSR_TDATA1);
	if (tdata1)
		csr_write(CSR_TDATA1,
			  tdata1 & SHIFT_UL(0xf, TDATA1_TYPE_SHIFT));
	csr_write(CSR_TDATA2, 0);
}

void dbtr_hart_init(void)
{
	struct dbtr_hart *d = this_dbtr();
	unsigned long val = 0;
	bool has_tinfo = false;

	*d = (struct dbtr_hart){ .shmem = SHMEM_NONE };

	/* No Sdtrig: tselect traps. Otherwise it only holds valid indices. */
	for (unsigned int i = 0; i < MAX_TRIGGERS; i++) {
		if (!may_trap(csr_write(CSR_TSELECT, i)) ||
		    csr_read(CSR_TSELECT) != i)
			break;
		/*
		 * tinfo is optional: the type in tdata1 then is the only one.
		 */
		has_tinfo = may_trap(val = csr_read(CSR_TINFO));
		if (has_tinfo) {
			d->types[i] = (uint32_t)val & 0xffff;
		} else {
			val = csr_read(CSR_TDATA1);
			d->types[i] = (uint32_t)BIT(TDATA1_TYPE(val));
		}
		if (d->types[i] == BIT(0))
			break; /* type 0: there is no trigger here */
		hw_clear(i);
		d->count = i + 1;
	}
	if (d->count) {
		csr_write(CSR_TSELECT, 0);
		d->has_tdata3 = may_trap(val = csr_read(CSR_TDATA3));
	}
}

unsigned long dbtr_num_triggers(unsigned long tdata1)
{
	struct dbtr_hart *d = this_dbtr();
	unsigned long n = 0;

	if (!tdata1)
		return d->count;
	for (unsigned int i = 0; i < d->count; i++)
		n += (d->types[i] >> TDATA1_TYPE(tdata1)) & 1;
	return n;
}

long dbtr_set_shmem(unsigned long lo, unsigned long hi, unsigned long flags)
{
	struct dbtr_hart *d = this_dbtr();

	if (flags)
		return SBI_ERR_INVALID_PARAM;
	if (lo == ~UL(0) && hi == ~UL(0)) {
		d->shmem = SHMEM_NONE;
		return SBI_SUCCESS;
	}
	if (!IS_ALIGNED(lo, sizeof(long)))
		return SBI_ERR_INVALID_PARAM;
	if (hi || !smode_range_ok(lo, d->count * sizeof(struct dbtr_entry)))
		return SBI_ERR_INVALID_ADDRESS;
	d->shmem = lo;
	return SBI_SUCCESS;
}

/* SBI_SUCCESS, or why the configuration cannot be a trigger of S-mode. */
static long config_check(const struct dbtr_entry *cfg)
{
	unsigned long type = TDATA1_TYPE(cfg->tdata1);

	if ((cfg->tdata1 & TDATA1_DMODE) || (cfg->tdata1 & MC_M))
		return SBI_ERR_INVALID_PARAM;
	if (type != TYPE_MCONTROL && type != TYPE_MCONTROL6)
		return SBI_ERR_NOT_SUPPORTED;
	return SBI_SUCCESS;
}

/* false: the hardware did not take the configuration as it is (WARL). */
static bool hw_program(struct dbtr_hart *d, unsigned int idx,
		       const struct dbtr_entry *cfg)
{
	unsigned long got = 0;

	/* Off while it changes, on with the last write. */
	hw_clear(idx);
	csr_write(CSR_TDATA2, cfg->tdata2);
	if (d->has_tdata3)
		csr_write(CSR_TDATA3, cfg->tdata3);
	else if (cfg->tdata3)
		return false;
	csr_write(CSR_TDATA1, cfg->tdata1);

	got = csr_read(CSR_TDATA1);
	if ((got ^ cfg->tdata1) & ~hit_bits(cfg->tdata1)) {
		hw_clear(idx);
		return false;
	}
	return true;
}

static unsigned long state_of(unsigned int idx, unsigned long tdata1)
{
	unsigned long state = STATE_MAPPED | STATE_HAVE_HW |
			      SHIFT_UL(idx, STATE_HW_IDX_SHIFT);

	if (tdata1 & MC_U)
		state |= STATE_U;
	if (tdata1 & MC_S)
		state |= STATE_S;
	if (TDATA1_TYPE(tdata1) == TYPE_MCONTROL6) {
		if (tdata1 & MC6_VU)
			state |= STATE_VU;
		if (tdata1 & MC6_VS)
			state |= STATE_VS;
	}
	return state;
}

/* Common entry checks of the shared memory calls. */
static long entries_get(struct dbtr_hart *d, unsigned long count,
			struct dbtr_entry **entries)
{
	/* Set, and still this domain's: the hart may have changed hands. */
	if (d->shmem == SHMEM_NONE ||
	    !smode_range_ok(d->shmem, d->count * sizeof(struct dbtr_entry)))
		return SBI_ERR_NO_SHMEM;
	if (count > d->count)
		return SBI_ERR_BAD_RANGE;
	/* Every caller pairs this with entries_put(). */
	*entries = smode_access_begin(d->shmem, d->count * sizeof(**entries));
	return SBI_SUCCESS;
}

static long entries_put(long rc)
{
	smode_access_end();
	return rc;
}

long dbtr_read(unsigned long base, unsigned long count)
{
	struct dbtr_hart *d = this_dbtr();
	struct dbtr_entry *out = NULL;
	long rc = entries_get(d, count, &out);

	if (rc)
		return rc;
	if (base >= d->count || count > d->count - base)
		return entries_put(SBI_ERR_BAD_RANGE);

	for (unsigned long i = 0; i < count; i++) {
		csr_write(CSR_TSELECT, base + i);
		out[i] = (struct dbtr_entry){
			.idx = d->state[base + i],
			.tdata1 = csr_read(CSR_TDATA1),
			.tdata2 = csr_read(CSR_TDATA2),
			.tdata3 = d->has_tdata3 ? csr_read(CSR_TDATA3) : 0,
		};
	}
	return entries_put(SBI_SUCCESS);
}

/*
 * First free run of hardware triggers that can take configurations
 * [first, first + len) (a chain, or a single one), -1 if there is none.
 */
static int find_run(const struct dbtr_hart *d, const struct dbtr_entry *cfg,
		    unsigned long len, const bool *taken)
{
	for (unsigned int start = 0; start + len <= d->count; start++) {
		unsigned long i = 0;

		for (i = 0; i < len; i++)
			if ((d->state[start + i] & STATE_MAPPED) ||
			    taken[start + i] ||
			    !((d->types[start + i] >>
			       TDATA1_TYPE(cfg[i].tdata1)) &
			      1))
				break;
		if (i == len)
			return (int)start;
	}
	return -1;
}

long dbtr_install(unsigned long count, unsigned long *failed)
{
	struct dbtr_hart *d = this_dbtr();
	struct dbtr_entry cfg[MAX_TRIGGERS] = {}, *mem = NULL;
	unsigned int where[MAX_TRIGGERS] = {};
	bool taken[MAX_TRIGGERS] = { false };
	long rc = entries_get(d, count, &mem);

	*failed = 0;
	if (rc)
		return rc;

	/* The memory is S-mode's: work on a copy. Validate, then place. */
	for (unsigned long i = 0; i < count; i++) {
		cfg[i] = mem[i];
		rc = config_check(&cfg[i]);
		if (!rc && i == count - 1 && (cfg[i].tdata1 & MC_CHAIN))
			rc = SBI_ERR_INVALID_PARAM;
		if (rc) {
			*failed = i;
			return entries_put(rc);
		}
	}
	for (unsigned long i = 0, len; i < count; i += len) {
		int start = 0;

		for (len = 1; cfg[i + len - 1].tdata1 & MC_CHAIN; len++)
			;
		start = find_run(d, &cfg[i], len, taken);
		if (start < 0) {
			*failed = i;
			return entries_put(SBI_ERR_FAILED);
		}
		for (unsigned long k = 0; k < len; k++) {
			where[i + k] = (unsigned int)start + (unsigned int)k;
			taken[where[i + k]] = true;
		}
	}

	for (unsigned long i = 0; i < count; i++) {
		if (!hw_program(d, where[i], &cfg[i])) {
			/* All or nothing: give back what this call took. */
			*failed = i;
			while (i--) {
				hw_clear(where[i]);
				d->state[where[i]] = 0;
			}
			return entries_put(SBI_ERR_NOT_SUPPORTED);
		}
		d->state[where[i]] = state_of(where[i], cfg[i].tdata1);
	}
	for (unsigned long i = 0; i < count; i++)
		mem[i].idx = where[i];
	return entries_put(SBI_SUCCESS);
}

long dbtr_update(unsigned long count, unsigned long *failed)
{
	struct dbtr_hart *d = this_dbtr();
	struct dbtr_entry cfg = {}, *mem = NULL;
	long rc = entries_get(d, count, &mem);

	*failed = 0;
	if (rc)
		return rc;

	for (unsigned long i = 0; i < count; i++) {
		unsigned long cur = 0;

		cfg = mem[i];
		*failed = i;
		rc = config_check(&cfg);
		if (rc)
			return entries_put(rc);
		if (cfg.idx >= d->count)
			return entries_put(SBI_ERR_INVALID_PARAM);
		if (!(d->state[cfg.idx] & STATE_MAPPED))
			return entries_put(SBI_ERR_FAILED);

		/*
		 * An update keeps the kind of trigger and its place in a chain.
		 */
		csr_write(CSR_TSELECT, cfg.idx);
		cur = csr_read(CSR_TDATA1);
		if (TDATA1_TYPE(cur) != TDATA1_TYPE(cfg.tdata1) ||
		    ((cur ^ cfg.tdata1) & MC_CHAIN))
			return entries_put(SBI_ERR_INVALID_PARAM);
		if (!hw_program(d, (unsigned int)cfg.idx, &cfg)) {
			d->state[cfg.idx] = 0;
			return entries_put(SBI_ERR_NOT_SUPPORTED);
		}
		d->state[cfg.idx] = state_of((unsigned int)cfg.idx, cfg.tdata1);
	}
	*failed = 0;
	return entries_put(SBI_SUCCESS);
}

enum set_op { SET_UNINSTALL, SET_ENABLE, SET_DISABLE };

static long set_op_apply(unsigned long base, unsigned long mask, enum set_op op)
{
	struct dbtr_hart *d = this_dbtr();

	/* Every trigger of the set has to be an installed one. */
	for (unsigned int i = 0; i < __RISCV_XLEN__; i++)
		if ((mask & BIT(i)) &&
		    (base + i < base || base + i >= d->count ||
		     !(d->state[base + i] & STATE_MAPPED)))
			return SBI_ERR_INVALID_PARAM;

	for (unsigned int i = 0; i < __RISCV_XLEN__; i++) {
		unsigned long idx = base + i, state = 0, tdata1 = 0, modes = 0;

		if (!(mask & BIT(i)))
			continue;
		if (op == SET_UNINSTALL) {
			hw_clear((unsigned int)idx);
			d->state[idx] = 0;
			continue;
		}

		state = d->state[idx];
		csr_write(CSR_TSELECT, idx);
		tdata1 = csr_read(CSR_TDATA1);
		if (op == SET_ENABLE) {
			modes |= state & STATE_U ? MC_U : 0;
			modes |= state & STATE_S ? MC_S : 0;
			modes |= state & STATE_VU ? MC6_VU : 0;
			modes |= state & STATE_VS ? MC6_VS : 0;
		}
		csr_write(CSR_TDATA1, (tdata1 & ~mode_bits(tdata1)) | modes);
	}
	return SBI_SUCCESS;
}

long dbtr_uninstall(unsigned long base, unsigned long mask)
{
	return set_op_apply(base, mask, SET_UNINSTALL);
}

long dbtr_enable(unsigned long base, unsigned long mask)
{
	return set_op_apply(base, mask, SET_ENABLE);
}

long dbtr_disable(unsigned long base, unsigned long mask)
{
	return set_op_apply(base, mask, SET_DISABLE);
}
