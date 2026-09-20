// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/pmu.h>
#include <arch/rfence.h>
#include <atomic.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <util.h>

static const struct ipi_ops *ipi;

void ipi_register(const struct ipi_ops *ops)
{
	if (!ipi || ops->rating > ipi->rating)
		ipi = ops;
}

unsigned long ipi_irq(void)
{
	return ipi ? ipi->irq : 0;
}

bool ipi_available(void)
{
	return ipi;
}

const char *ipi_name(void)
{
	return ipi ? ipi->name : "none";
}

void ipi_hart_init(void)
{
	struct hart *h = this_hart();

	if (!ipi)
		return;
	if (ipi->hart_init)
		ipi->hart_init();
	ipi->clear(h->index);
	atomic_store_ulong(&h->ipi_pending, 0);
	csr_set(mie, ipi->irq);
}

void ipi_kick(unsigned int index)
{
	if (ipi && hart_by_index(index))
		ipi->send(index);
}

void ipi_send(unsigned int index, enum ipi_event event)
{
	struct hart *h = hart_by_index(index);

	if (!ipi || !h)
		return;
	if (event == IPI_EVENT_SMODE)
		pmu_fw_event(SBI_PMU_FW_IPI_SENT);
	atomic_or_ulong(&h->ipi_pending, BIT(event));
	ipi->send(index);
}

void ipi_send_mask(const struct hartmask *mask, enum ipi_event event)
{
	unsigned int index = 0;

	for_each_hart_in_mask(index, mask)
		ipi_send(index, event);
}

void ipi_process(void)
{
	struct hart *h = this_hart();
	unsigned long pending = 0;

	if (!ipi)
		return;

	/*
	 * Clear first: an event posted after the read re-raises the interrupt.
	 */
	ipi->clear(h->index);
	pending = atomic_swap_ulong(&h->ipi_pending, 0);

	if (pending & BIT(IPI_EVENT_HALT))
		hart_halt();
	if (pending & BIT(IPI_EVENT_RFENCE))
		rfence_process();
	if (pending & BIT(IPI_EVENT_SMODE)) {
		pmu_fw_event(SBI_PMU_FW_IPI_RECEIVED);
		csr_set(mip, MIP_SSIP);
	}
}
