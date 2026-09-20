// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Monitor trap policy. From S/U-mode: ecalls go to the service layer,
 * M-mode timer and software interrupts to their cores, illegal
 * instructions and misaligned accesses to their emulators; every other
 * exception is handed back to S-mode. A trap from M-mode itself is fatal unless
 * the hart announced it (CSR probing, unprivileged access).
 */

#include <arch/hart.h>
#include <arch/pmu.h>
#include <arch/sse.h>
#include <arch/trap.h>
#include <domain.h>
#include <ipi.h>
#include <log.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <service.h>
#include <timer.h>
#include <util.h>

static void trap_info_read(const struct trap_regs *regs, struct trap_info *info)
{
	info->cause = csr_read(mcause);
	info->tval = csr_read(mtval);
	info->tval2 = 0;
	info->tinst = 0;
	info->gva = false;
	info->virt = false;

	if (!hart_has(HART_FEAT_H))
		return;
	info->tval2 = csr_read(CSR_MTVAL2);
	info->tinst = csr_read(CSR_MTINST);
#if __RISCV_XLEN__ == 64
	info->gva = regs->mstatus & MSTATUS_GVA;
	info->virt = regs->mstatus & MSTATUS_MPV;
#else
	info->gva = csr_read(CSR_MSTATUSH) & MSTATUSH_GVA;
	info->virt = csr_read(CSR_MSTATUSH) & MSTATUSH_MPV;
#endif
}

/*
 * A double trap in S-mode (Ssdbltrp): S-mode trapped while it could not
 * take a trap (sstatus.SDT). Its only way out is the SSE double trap event,
 * if it has one ready; otherwise this hart has nowhere to go.
 */
static void trap_double(struct trap_regs *regs)
{
	if (sse_raise_local(SSE_EVENT_LOCAL_DOUBLE_TRAP))
		return;
	pr_err("hart %lu: double trap in S-mode at %lx, no handler: halted\n",
	       this_hartid(), regs->mepc);
	hart_halt();
}

void trap_redirect(struct trap_regs *regs, const struct trap_info *info)
{
	unsigned long prev = get_field_ul(regs->mstatus, MSTATUS_MPP);
	unsigned long mstatus = regs->mstatus;

	if (prev == PRV_M)
		trap_fatal(regs, "cannot redirect an M-mode trap");

	/* What the hardware does for a trap it delivers to S-mode itself. */
	if (hart_smode_double_trap_enabled()) {
		if (mstatus & MSTATUS_SDT) {
			trap_double(regs);
			return;
		}
		mstatus |= MSTATUS_SDT;
	}

	if (hart_has(HART_FEAT_H)) {
		/*
		 * The trap lands in HS-mode: record the virtualisation state.
		 */
		unsigned long hstatus = csr_read(CSR_HSTATUS);

		hstatus &= ~(HSTATUS_SPV | HSTATUS_GVA);
		if (info->virt) {
			hstatus |= HSTATUS_SPV;
			hstatus &= ~HSTATUS_SPVP;
			if (prev == PRV_S)
				hstatus |= HSTATUS_SPVP;
		}
		if (info->gva)
			hstatus |= HSTATUS_GVA;
		csr_write(CSR_HSTATUS, hstatus);
		csr_write(CSR_HTVAL, info->tval2);
		csr_write(CSR_HTINST, info->tinst);
#if __RISCV_XLEN__ == 64
		mstatus &= ~(MSTATUS_MPV | MSTATUS_GVA);
#else
		csr_clear(CSR_MSTATUSH, MSTATUSH_MPV | MSTATUSH_GVA);
#endif
	}

	csr_write(scause, info->cause);
	csr_write(stval, info->tval);
	csr_write(sepc, regs->mepc);

	/* What the hardware does on a trap into S-mode. */
	mstatus &= ~(MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_MPP);
	if (prev == PRV_S)
		mstatus |= MSTATUS_SPP;
	if (mstatus & MSTATUS_SIE)
		mstatus |= MSTATUS_SPIE;
	mstatus &= ~MSTATUS_SIE;
	mstatus |= SHIFT_UL(PRV_S, MSTATUS_MPP_SHIFT);

	regs->mstatus = mstatus;
	regs->mepc = csr_read(stvec) & ~UL(3);
}

/* A trap from S/U-mode (or VS/VU-mode). */
static void trap_from_below(struct trap_regs *regs)
{
	struct trap_info info = {};

	trap_info_read(regs, &info);

	if (info.cause & CAUSE_IRQ_FLAG) {
		unsigned long irq = info.cause & ~CAUSE_IRQ_FLAG;

		/*
		 * The IPI: a software interrupt, or an MSI through the IMSIC.
		 */
		if (irq == IRQ_M_TIMER)
			timer_process();
		else if (irq == IRQ_PMU_OVF && pmu_sse_overflow_irq())
			sse_raise_local(SSE_EVENT_LOCAL_PMU_OVERFLOW);
		else if (irq < __RISCV_XLEN__ && BIT(irq) == ipi_irq())
			ipi_process();
		else
			trap_fatal(regs, "unhandled interrupt");
		return;
	}

	switch (info.cause) {
	case CAUSE_SUPERVISOR_ECALL:
		/* A service may redirect or restart the hart: advance first. */
		regs->mepc += 4;
		service_ecall(regs);
		return;
	case CAUSE_ILLEGAL_INSN:
		trap_illegal_insn(regs, &info);
		return;
	case CAUSE_DOUBLE_TRAP:
		trap_double(regs);
		return;
	case CAUSE_MISALIGNED_LOAD:
		pmu_fw_event(SBI_PMU_FW_MISALIGNED_LOAD);
		trap_misaligned(regs, &info);
		return;
	case CAUSE_MISALIGNED_STORE:
		pmu_fw_event(SBI_PMU_FW_MISALIGNED_STORE);
		trap_misaligned(regs, &info);
		return;
	case CAUSE_LOAD_ACCESS:
		pmu_fw_event(SBI_PMU_FW_ACCESS_LOAD);
		break;
	case CAUSE_STORE_ACCESS:
		pmu_fw_event(SBI_PMU_FW_ACCESS_STORE);
		break;
	default:
		break;
	}
	trap_redirect(regs, &info);
}

void trap_handler(struct trap_regs *regs)
{
	struct hart *h = this_hart();

	if ((regs->mstatus & MSTATUS_MPP) == MSTATUS_MPP) {
		if (h && h->trap_expected) {
			h->trap_taken = 1;
			h->trap_cause = csr_read(mcause);
			h->trap_tval = csr_read(mtval);
			regs->mepc += 4;
			return;
		}
		trap_fatal(regs, "unexpected trap in M-mode");
	}

	trap_from_below(regs);
	/* Its domain is being stopped: this is as far as S-mode got. */
	if (domain_stop_pending())
		domain_stop_self(regs);
	/*
	 * Notification events that came in meanwhile: S-mode is told on its way
	 * back.
	 */
	mpxy_indicate();
	/*
	 * Whatever the return to S-mode looks like now, an event goes on top.
	 */
	sse_process(regs);
}
