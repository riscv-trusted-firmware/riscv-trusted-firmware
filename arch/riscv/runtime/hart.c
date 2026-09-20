// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Per-hart state, feature detection and the M-mode configuration a hart
 * needs before it can run a lower privilege level.
 */

#include <arch/hart.h>
#include <arch/dbtr.h>
#include <arch/fwft.h>
#include <arch/image.h>
#include <arch/pmp.h>
#include <arch/pmu.h>
#include <arch/sse.h>
#include <atomic.h>
#include <boot.h>
#include <domain.h>
#include <io.h>
#include <ipi.h>
#include <log.h>
#include <memregion.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <timer.h>
#include <types_ext.h>
#include <util.h>

extern char __stack_top[];

static struct hart harts[CONFIG_PLATFORM_HART_COUNT];
static bool features[HART_FEAT_COUNT];

/*
 * The address map as far as protection goes (<memregion.h>). The monitor's
 * own image is added at boot, the rest by the drivers.
 */
#define MAX_MEMREGIONS 12

static struct memregion {
	paddr_t base;
	paddr_size_t size;
	enum memregion_kind kind;
} memregions[MAX_MEMREGIONS];
static unsigned int nr_memregions;

void memregion_add(paddr_t base, paddr_size_t size, enum memregion_kind kind)
{
	/* Neighbours of a kind share an entry (the CLINT's MSWI and MTIMER). */
	for (unsigned int i = 0; i < nr_memregions; i++) {
		struct memregion *r = &memregions[i];

		if (r->kind != kind || base > r->base + r->size ||
		    r->base > base + size)
			continue;
		if (base + size > r->base + r->size)
			r->size = base + size - r->base;
		if (base < r->base) {
			r->size += r->base - base;
			r->base = base;
		}
		return;
	}
	if (nr_memregions == MAX_MEMREGIONS)
		panic("too many memory regions\n");
	memregions[nr_memregions++] = (struct memregion){ base, size, kind };
}

struct hart *hart_by_index(unsigned int index)
{
	return index < _boot_hart_nr ? &harts[index] : NULL;
}

int hart_index(unsigned long hartid)
{
	return boot_hart_index(hartid);
}

struct hart *hart_get(unsigned long hartid)
{
	int index = boot_hart_index(hartid);

	return index < 0 ? NULL : &harts[index];
}

unsigned long hart_id_of(unsigned int index)
{
	return index < _boot_hart_nr ? _boot_hart_ids[index] : ~UL(0);
}

bool hart_index_valid(unsigned int index)
{
	return index < _boot_hart_nr &&
	       atomic_load_ulong(&harts[index].present);
}

bool hart_valid(unsigned long hartid)
{
	struct hart *h = hart_get(hartid);

	return h && atomic_load_ulong(&h->present);
}

unsigned int hart_count(void)
{
	unsigned int n = 0;

	for (unsigned int i = 0; i < _boot_hart_nr; i++)
		n += atomic_load_ulong(&harts[i].present) != 0;
	return n;
}

void hart_init(unsigned long hartid)
{
	/* entry.S gave this hart a stack, so it is in the table. */
	unsigned int index = (unsigned int)boot_hart_index(hartid);
	struct hart *h = &harts[index];

	h->hartid = hartid;
	h->index = index;
	h->m_sp = (unsigned long)__stack_top - index * CONFIG_STACK_SIZE;
	/*
	 * Not its first time here: a hart the platform took down and brought
	 * back.
	 */
	if (!atomic_load_ulong(&h->present))
		atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STOPPED);
	__asm__ __volatile__("mv tp, %0" : : "r"(h));
	atomic_store_ulong(&h->present, 1);
}

bool hart_has(enum hart_feature feat)
{
	return features[feat];
}

vaddr_t monitor_base(void)
{
	return (unsigned long)__image_start;
}

/* Boot hart: the monitor's own image. */
static void monitor_regions_init(void)
{
	unsigned long start = monitor_base();
	unsigned long split = (unsigned long)__text_rodata_end;

	/* One entry keeps S-mode out; confining M-mode takes the W^X split. */
	if (!hart_has(HART_FEAT_SMEPMP)) {
		memregion_add(start, CONFIG_MONITOR_SIZE, MEMREGION_MMODE_RW);
		return;
	}
	memregion_add(start, split - start, MEMREGION_MMODE_RX);
	memregion_add(split, start + CONFIG_MONITOR_SIZE - split,
		      MEMREGION_MMODE_RW);
}

void hart_detect_features(void)
{
	unsigned long val = 0;

	features[HART_FEAT_H] = csr_read(misa) & MISA_EXT('H');
	features[HART_FEAT_TIME_CSR] = csr_probe(CSR_TIME, &val);
	features[HART_FEAT_MENVCFG] = csr_probe(CSR_MENVCFG, &val);

	/* pmpaddr is WARL: an unimplemented entry reads back as zero. */
	if (CONFIG_RISCV_PMP_COUNT >= 2 && csr_probe(CSR_PMPADDR0, &val)) {
		csr_write(CSR_PMPADDR0, ~UL(0));
		features[HART_FEAT_PMP] = csr_read(CSR_PMPADDR0) != 0;
		csr_write(CSR_PMPADDR0, val);
	}

#ifdef CONFIG_RISCV_EXT_SSTC
	/* stimecmp exists when Sstc does; menvcfg.STCE then sticks. */
	if (features[HART_FEAT_MENVCFG] && csr_probe(CSR_STIMECMP, &val))
		features[HART_FEAT_SSTC] = true;
#endif
	features[HART_FEAT_SDTRIG] = csr_probe(CSR_TSELECT, &val);
	if (features[HART_FEAT_MENVCFG]) {
		/* menvcfg.DTE is writable where Ssdbltrp exists. */
#if __RISCV_XLEN__ == 64
		val = csr_read(CSR_MENVCFG);
		csr_set(CSR_MENVCFG, BIT(ENVCFG_DTE_BIT));
		features[HART_FEAT_SSDBLTRP] = csr_read(CSR_MENVCFG) &
					       BIT(ENVCFG_DTE_BIT);
		csr_write(CSR_MENVCFG, val);
#else
		val = csr_read(CSR_MENVCFGH);
		csr_set(CSR_MENVCFGH, BIT(ENVCFG_DTE_BIT - 32));
		features[HART_FEAT_SSDBLTRP] = csr_read(CSR_MENVCFGH) &
					       BIT(ENVCFG_DTE_BIT - 32);
		csr_write(CSR_MENVCFGH, val);
#endif
	}
#ifdef CONFIG_RISCV_EXT_SMEPMP
	/* mseccfg exists when Smepmp does. */
	features[HART_FEAT_SMEPMP] = features[HART_FEAT_PMP] &&
				     csr_probe(CSR_MSECCFG, &val);
#endif
#ifdef CONFIG_RISCV_EXT_SMSTATEEN
	features[HART_FEAT_SMSTATEEN] = csr_probe(CSR_MSTATEEN0, &val);
#endif
#ifdef CONFIG_RISCV_EXT_SSCOFPMF
	/* scountovf exists when Sscofpmf does. */
	features[HART_FEAT_SSCOFPMF] = csr_probe(CSR_SCOUNTOVF, &val);
#endif

	monitor_regions_init();
}

bool memregion_get(unsigned int i, paddr_t *base, paddr_size_t *size,
		   enum memregion_kind *kind)
{
	if (i >= nr_memregions)
		return false;
	*base = memregions[i].base;
	*size = memregions[i].size;
	*kind = memregions[i].kind;
	return true;
}

bool monitor_range_clear(paddr_t addr, paddr_size_t size)
{
	unsigned long end = addr + size;

	if (end < addr)
		return false;
	for (unsigned int i = 0; i < nr_memregions; i++)
		if (memregions[i].kind != MEMREGION_SHARED_RW &&
		    end > memregions[i].base &&
		    addr < memregions[i].base + memregions[i].size)
			return false;
	return true;
}

#ifdef CONFIG_DOMAINS
bool smode_range_ok(paddr_t addr, paddr_size_t size)
{
	return domain_range_ok(this_domain(), addr, size,
			       DOMAIN_PERM_SU_R | DOMAIN_PERM_SU_W);
}

bool smode_range_readable(paddr_t addr, paddr_size_t size)
{
	return domain_range_ok(this_domain(), addr, size, DOMAIN_PERM_SU_R);
}

bool smode_entry_ok(paddr_t addr)
{
	return domain_range_ok(this_domain(), addr, 4, DOMAIN_PERM_SU_X);
}
#else
bool smode_range_readable(paddr_t addr, paddr_size_t size)
{
	return monitor_range_clear(addr, size);
}

bool smode_range_ok(paddr_t addr, paddr_size_t size)
{
	return monitor_range_clear(addr, size);
}

bool smode_entry_ok(paddr_t addr)
{
	return monitor_range_clear(addr, 4);
}
#endif

bool hart_smode_double_trap_enabled(void)
{
	if (!hart_has(HART_FEAT_SSDBLTRP))
		return false;
#if __RISCV_XLEN__ == 64
	return csr_read(CSR_MENVCFG) & BIT(ENVCFG_DTE_BIT);
#else
	return csr_read(CSR_MENVCFGH) & BIT(ENVCFG_DTE_BIT - 32);
#endif
}

static void envcfg_init(void)
{
	if (!hart_has(HART_FEAT_MENVCFG))
		return;

	/*
	 * Start from zero (the reset value of what S-mode controls through
	 * FWFT), then WARL: bits of extensions the hart lacks stay clear.
	 */
	csr_write(CSR_MENVCFG, 0);
#if __RISCV_XLEN__ == 32
	csr_write(CSR_MENVCFGH, 0);
#endif
	csr_set(CSR_MENVCFG, ENVCFG_CBIE | ENVCFG_CBCFE | ENVCFG_CBZE);

	if (hart_has(HART_FEAT_SSTC)) {
		/* No timer interrupt until S-mode programs stimecmp. */
		csr_write(CSR_STIMECMP, ~UL(0));
#if __RISCV_XLEN__ == 32
		csr_write(CSR_STIMECMPH, ~UL(0));
#endif
	}
#if __RISCV_XLEN__ == 64
	csr_set(CSR_MENVCFG, BIT(ENVCFG_PBMTE_BIT));
	if (hart_has(HART_FEAT_SSTC))
		csr_set(CSR_MENVCFG, BIT(ENVCFG_STCE_BIT));
#else
	csr_set(CSR_MENVCFGH, BIT(ENVCFG_PBMTE_BIT - 32));
	if (hart_has(HART_FEAT_SSTC))
		csr_set(CSR_MENVCFGH, BIT(ENVCFG_STCE_BIT - 32));
#endif
}

/*
 * State enables exist to keep state away from a lower privilege level. The
 * monitor has no such policy: S-mode gets what it would have on a hart
 * without Smstateen (WARL, so bits without a meaning stay clear).
 */
static void stateen_init(void)
{
	if (!hart_has(HART_FEAT_SMSTATEEN))
		return;
	csr_write(CSR_MSTATEEN0, ~UL(0));
	csr_write(CSR_MSTATEEN0 + 1, ~UL(0));
	csr_write(CSR_MSTATEEN0 + 2, ~UL(0));
	csr_write(CSR_MSTATEEN0 + 3, ~UL(0));
#if __RISCV_XLEN__ == 32
	csr_write(CSR_MSTATEEN0H, ~UL(0));
	csr_write(CSR_MSTATEEN0H + 1, ~UL(0));
	csr_write(CSR_MSTATEEN0H + 2, ~UL(0));
	csr_write(CSR_MSTATEEN0H + 3, ~UL(0));
#endif
}

/*
 * PMP layout. The first matching entry decides:
 *
 *   0, 1     Smepmp only: the window M-mode opens on S-mode memory
 *   ...      the memory regions, a NAPOT entry or an OFF + TOR pair each
 *   ...      what S/U-mode gets: everything else RWX, or with domains the
 *            regions of the domain the hart runs, smallest first
 *
 * Without Smepmp an M-mode region is an entry without permissions: S/U-mode
 * is kept out and M-mode is not bound by it. With Smepmp (mseccfg.MML)
 * M-mode is confined as well: its regions are locked rules (R-X for the
 * monitor's code and constants, RW otherwise, so the monitor is W^X), the
 * devices it shares with S-mode are shared rules, and with MMWP anything
 * else is out of its reach, S-mode memory included.
 */
#define SMEPMP_WINDOW_ENTRIES 2

static bool region_is_napot(const struct memregion *r)
{
	return r->size >= 8 && IS_POWER_OF_TWO(r->size) &&
	       IS_ALIGNED(r->base, r->size);
}

/* The first entry past the monitor's own. */
static unsigned int pmp_monitor_entries(void)
{
	unsigned int n = hart_has(HART_FEAT_SMEPMP) ? SMEPMP_WINDOW_ENTRIES : 0;

	for (unsigned int i = 0; i < nr_memregions; i++)
		if (memregions[i].kind != MEMREGION_SHARED_RW ||
		    hart_has(HART_FEAT_SMEPMP))
			n += region_is_napot(&memregions[i]) ? 1 : 2;
	return n;
}

static unsigned int region_cfg(enum memregion_kind kind)
{
	if (!hart_has(HART_FEAT_SMEPMP))
		return 0;
	switch (kind) {
	case MEMREGION_MMODE_RX:
		return PMP_L | PMP_R | PMP_X;
	case MEMREGION_MMODE_RW:
		return PMP_L | PMP_R | PMP_W;
	default:
		/* MML: L=0, RWX=011 is "read-write for M-mode and S/U-mode". */
		return PMP_W | PMP_X;
	}
}

void pmp_hart_init(void)
{
	unsigned int idx = 0;

	if (!hart_has(HART_FEAT_PMP)) {
		pr_warn("hart %lu: no PMP, firmware memory is not protected\n",
			this_hartid());
		return;
	}

	/*
	 * A hart that is started again already lives by these rules, and
	 * rewriting the one it executes from would pull it away under its feet.
	 */
	if (hart_has(HART_FEAT_SMEPMP) &&
	    (csr_read(CSR_MSECCFG) & MSECCFG_MML)) {
		pmp_domain_set();
		return;
	}

	if (hart_has(HART_FEAT_SMEPMP)) {
		/*
		 * Rule locking bypass, and it stays on: the window on S-mode
		 * memory is a shared-region rule written at run time, which
		 * implementations (QEMU) only take under MML with RLB set.
		 * It can only be set while no rule is locked.
		 */
		csr_set(CSR_MSECCFG, MSECCFG_RLB);
		pmp_entry_set(0, 0, 0);
		pmp_entry_set(1, 0, 0);
		idx = SMEPMP_WINDOW_ENTRIES;
	}
	/*
	 * Two passes around the switch to MML: a locked rule with execute
	 * permission can only be written before it, and the shared-region
	 * encoding (W without R) only means something after it.
	 */
	for (int shared = 0; shared <= 1; shared++) {
		unsigned int at = idx;

		for (unsigned int i = 0; i < nr_memregions; i++) {
			const struct memregion *r = &memregions[i];
			bool is_shared = r->kind == MEMREGION_SHARED_RW;
			bool napot = region_is_napot(r);

			if (r->kind == MEMREGION_SHARED_RW &&
			    !hart_has(HART_FEAT_SMEPMP))
				continue;
			if (at + 3 > CONFIG_RISCV_PMP_COUNT)
				panic("out of PMP entries at region %lx+%lx\n",
				      r->base, r->size);
			if (is_shared == shared)
				pmp_range_set(at, r->base, r->size,
					      region_cfg(r->kind));
			at += napot ? 1 : 2;
		}
		/* Sticky until reset. */
		if (!shared && hart_has(HART_FEAT_SMEPMP))
			csr_set(CSR_MSECCFG, MSECCFG_MML | MSECCFG_MMWP);
	}
	pmp_domain_set();
}

unsigned int pmp_domain_entries(void)
{
	unsigned int used = pmp_monitor_entries();

	return used < CONFIG_RISCV_PMP_COUNT ? CONFIG_RISCV_PMP_COUNT - used :
					       0;
}

void pmp_domain_set(void)
{
	unsigned int at = pmp_monitor_entries();
#ifdef CONFIG_DOMAINS
	const struct domain *dom = this_domain();
#endif

	if (!hart_has(HART_FEAT_PMP))
		return;
#ifdef CONFIG_DOMAINS
	/*
	 * S/U-mode permissions only, which is what an unlocked rule is about
	 * with Smepmp and without: the monitor reaches S-mode memory through
	 * its window, or is not bound by these at all.
	 */
	for (unsigned int i = 0; i < dom->nr_regions; i++) {
		const struct domain_region *r = &dom->regions[i];
		unsigned int su = r->perm >> DOMAIN_PERM_SU_SHIFT;
		unsigned int cfg = (su & 1 ? PMP_R : 0) | (su & 2 ? PMP_W : 0) |
				   (su & 4 ? PMP_X : 0);

		/* domains_start() has checked that every domain fits. */
		if (at == CONFIG_RISCV_PMP_COUNT)
			break;
		/*
		 * W without R is reserved, or another thing altogether (MML).
		 */
		if ((cfg & PMP_W) && !(cfg & PMP_R))
			cfg &= ~(unsigned int)PMP_W;
		if (r->order >= __RISCV_XLEN__)
			pmp_all_set(at++, cfg);
		else
			at += pmp_range_set(at, r->base, BIT(r->order), cfg);
	}
#else
	pmp_all_set(at++, PMP_R | PMP_W | PMP_X);
#endif
	/* What a domain with more regions left behind. */
	while (at < CONFIG_RISCV_PMP_COUNT)
		pmp_entry_set(at++, 0, 0);
	__asm__ __volatile__("sfence.vma" ::: "memory");
}

/*
 * The window each hart has open, for smode_poke32() to put back; size 0: none.
 */
static struct {
	unsigned long addr, size;
} windows[CONFIG_PLATFORM_HART_COUNT];

void *smode_access_begin(paddr_t addr, paddr_size_t size)
{
	windows[this_hart_index()].addr = addr;
	windows[this_hart_index()].size = size;
	if (hart_has(HART_FEAT_SMEPMP) && size) {
		pmp_entry_set(0, addr >> 2, 0);
		pmp_entry_set(1, (addr + size + 3) >> 2,
			      PMP_A_TOR | PMP_W | PMP_X);
	}
	return (void *)addr;
}

void smode_access_end(void)
{
	windows[this_hart_index()].size = 0;
	if (hart_has(HART_FEAT_SMEPMP))
		pmp_entry_cfg(1, 0);
}

void smode_poke32(paddr_t addr, uint32_t val)
{
	unsigned long outer_addr = windows[this_hart_index()].addr;
	unsigned long outer_size = windows[this_hart_index()].size;

	io_write32((vaddr_t)smode_access_begin(addr, 4), val);
	smode_access_end();
	if (outer_size)
		smode_access_begin(outer_addr, outer_size);
}

void hart_services_switch_out(void)
{
	pmu_hart_switch_out();
	dbtr_hart_switch_out();
	fwft_hart_switch_out();
}

void hart_services_switch_in(bool fresh)
{
	fwft_hart_switch_in(fresh);
	dbtr_hart_switch_in(fresh);
	pmu_hart_switch_in(fresh);
	sse_hart_switch_in(fresh);
}

void hart_runtime_init(void)
{
	unsigned long deleg = 0;

	/*
	 * Exceptions S-mode handles itself. Illegal instructions and access
	 * faults come to M-mode (emulation, then redirection); so do S-mode
	 * ecalls.
	 */
	deleg = BIT(CAUSE_MISALIGNED_FETCH) | BIT(CAUSE_BREAKPOINT) |
		BIT(CAUSE_USER_ECALL) | BIT(CAUSE_FETCH_PAGE_FAULT) |
		BIT(CAUSE_LOAD_PAGE_FAULT) | BIT(CAUSE_STORE_PAGE_FAULT);
	if (hart_has(HART_FEAT_H))
		deleg |= BIT(CAUSE_VSUPERVISOR_ECALL) |
			 BIT(CAUSE_FETCH_GUEST_PAGE_FAULT) |
			 BIT(CAUSE_LOAD_GUEST_PAGE_FAULT) |
			 BIT(CAUSE_VIRTUAL_INSN) |
			 BIT(CAUSE_STORE_GUEST_PAGE_FAULT);
	csr_write(medeleg, deleg);
	deleg = MIP_SSIP | MIP_STIP | MIP_SEIP;
	if (hart_has(HART_FEAT_SSCOFPMF))
		deleg |= BIT(IRQ_PMU_OVF);
	csr_write(mideleg, deleg);

	/*
	 * Counters: all of them, 'time' only when it does not need emulation.
	 */
	csr_write(mcounteren,
		  hart_has(HART_FEAT_TIME_CSR) ? ~UL(0) : ~COUNTEREN_TM);
	csr_write(scounteren, 0);

	envcfg_init();
	stateen_init();
	pmp_hart_init();
	pmu_hart_init();
	fwft_hart_init();
	sse_hart_init();
	dbtr_hart_init();
	mpxy_hart_init();

	/* Nothing stale pending when the next stage (re)starts on this hart. */
	csr_clear(mip, MIP_SSIP | MIP_STIP);

	timer_hart_init();
	ipi_hart_init();
}

void __noreturn hart_enter_smode(unsigned long entry, unsigned long arg0,
				 unsigned long arg1, unsigned long mode)
{
	unsigned long mstatus = csr_read(mstatus);

	/* Whatever ran in S-mode on this hart before is gone. */
	mstatus &= ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MPRV | MSTATUS_SIE |
		     MSTATUS_SPIE | MSTATUS_SPP);
	mstatus |= mode << MSTATUS_MPP_SHIFT;
#if __RISCV_XLEN__ == 64
	mstatus &= ~MSTATUS_MPV;
#else
	if (hart_has(HART_FEAT_H))
		csr_clear(CSR_MSTATUSH, MSTATUSH_MPV);
#endif
	csr_write(mstatus, mstatus);

	/* A defined S-mode state: no translation, no interrupts. */
	csr_write(stvec, entry);
	csr_write(sscratch, 0);
	csr_write(sie, 0);
	csr_write(satp, 0);

	_hart_mret(entry, arg0, arg1);
}

void __noreturn hart_halt(void)
{
	csr_write(mie, 0);
	for (;;)
		wfi();
}
