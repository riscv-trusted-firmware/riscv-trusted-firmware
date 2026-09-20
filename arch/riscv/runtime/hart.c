// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Per-hart state, feature detection and the M-mode configuration a hart
 * needs before it can run a lower privilege level.
 */

#include <arch/hart.h>
#include <arch/fwft.h>
#include <arch/pmp.h>
#include <arch/pmu.h>
#include <atomic.h>
#include <ipi.h>
#include <irqchip.h>
#include <log.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <timer.h>
#include <types_ext.h>
#include <util.h>

extern char __stack_top[];

static struct hart harts[CONFIG_PLATFORM_HART_COUNT];
static bool features[HART_FEAT_COUNT];

/* Memory S/U-mode is fenced off from: a PMP NAPOT entry each, in this order. */
static const struct {
	unsigned long base, size;
} mmode_regions[] = {
	{ CONFIG_MONITOR_LOAD_ADDR, CONFIG_MONITOR_SIZE },
#ifdef CONFIG_RPMI_SHMEM_PROTECT
	/* The transport to the platform microcontroller: all four queues. */
	{ CONFIG_RPMI_SHMEM_BASE, 4 * CONFIG_RPMI_SHMEM_QUEUE_SIZE },
#endif
};

struct hart *hart_get(unsigned long hartid)
{
	return hartid < CONFIG_PLATFORM_HART_COUNT ? &harts[hartid] : NULL;
}

bool hart_valid(unsigned long hartid)
{
	struct hart *h = hart_get(hartid);

	return h && atomic_load_ulong(&h->present);
}

unsigned int hart_count(void)
{
	unsigned int n = 0;

	for (unsigned long i = 0; i < CONFIG_PLATFORM_HART_COUNT; i++)
		n += hart_valid(i);
	return n;
}

void hart_init(unsigned long hartid)
{
	struct hart *h = &harts[hartid];

	h->hartid = hartid;
	h->m_sp = (unsigned long)__stack_top - hartid * CONFIG_STACK_SIZE;
	atomic_store_ulong(&h->hsm_state, SBI_HSM_STATE_STOPPED);
	__asm__ __volatile__("mv tp, %0" : : "r"(h));
	atomic_store_ulong(&h->present, 1);
}

bool hart_has(enum hart_feature feat)
{
	return features[feat];
}

void hart_detect_features(void)
{
	unsigned long val = 0;

	features[HART_FEAT_H] = csr_read(misa) & MISA_EXT('H');
	features[HART_FEAT_TIME_CSR] = csr_probe(CSR_TIME, &val);
	features[HART_FEAT_MENVCFG] = csr_probe(CSR_MENVCFG, &val);

	/* pmpaddr is WARL: an unimplemented entry reads back as zero. */
	if (CONFIG_RISCV_PMP_COUNT > (int)ARRAY_SIZE(mmode_regions) &&
	    csr_probe(CSR_PMPADDR0, &val)) {
		csr_write(CSR_PMPADDR0, ~UL(0));
		features[HART_FEAT_PMP] = csr_read(CSR_PMPADDR0) != 0;
		csr_write(CSR_PMPADDR0, val);
	}

#ifdef CONFIG_RISCV_EXT_SSTC
	/* stimecmp exists when Sstc does; menvcfg.STCE then sticks. */
	if (features[HART_FEAT_MENVCFG] && csr_probe(CSR_STIMECMP, &val))
		features[HART_FEAT_SSTC] = true;
#endif
#ifdef CONFIG_RISCV_EXT_SMSTATEEN
	features[HART_FEAT_SMSTATEEN] = csr_probe(CSR_MSTATEEN0, &val);
#endif
#ifdef CONFIG_RISCV_EXT_SSCOFPMF
	/* scountovf exists when Sscofpmf does. */
	features[HART_FEAT_SSCOFPMF] = csr_probe(CSR_SCOUNTOVF, &val);
#endif
}

bool smode_range_ok(paddr_t addr, paddr_size_t size)
{
	unsigned long end = addr + size;

	if (end < addr)
		return false;
	for (unsigned int i = 0; i < ARRAY_SIZE(mmode_regions); i++)
		if (end > mmode_regions[i].base &&
		    addr < mmode_regions[i].base + mmode_regions[i].size)
			return false;
	return true;
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

static void pmp_init(void)
{
	if (!hart_has(HART_FEAT_PMP)) {
		pr_warn("hart %lu: no PMP, firmware memory is not protected\n",
			this_hartid());
		return;
	}

	/*
	 * The first entries hide M-mode memory, the one after opens the rest.
	 */
	for (unsigned int i = 0; i < ARRAY_SIZE(mmode_regions); i++)
		if (pmp_set_napot(i, mmode_regions[i].base,
				  mmode_regions[i].size, 0))
			panic("M-mode region %lx+%lx is not a NAPOT range\n",
			      mmode_regions[i].base, mmode_regions[i].size);
	pmp_set_all(ARRAY_SIZE(mmode_regions), PMP_R | PMP_W | PMP_X);
	__asm__ __volatile__("sfence.vma" ::: "memory");
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
	pmp_init();
	pmu_hart_init();
	fwft_hart_init();
	irqchip_hart_init();
	mpxy_hart_init();

	/* Nothing stale pending when the next stage (re)starts on this hart. */
	csr_clear(mip, MIP_SSIP | MIP_STIP);

	timer_hart_init();
	ipi_hart_init();
}

void __noreturn hart_enter_smode(unsigned long entry, unsigned long arg0,
				 unsigned long arg1)
{
	unsigned long mstatus = csr_read(mstatus);

	/* Whatever ran in S-mode on this hart before is gone. */
	mstatus &= ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MPRV | MSTATUS_SIE |
		     MSTATUS_SPIE | MSTATUS_SPP);
	mstatus |= SHIFT_UL(PRV_S, MSTATUS_MPP_SHIFT);
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
