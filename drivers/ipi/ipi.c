// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/rfence.h>
#include <atomic.h>
#include <ipi.h>
#include <util.h>

static const struct ipi_ops *ipi;

void ipi_register(const struct ipi_ops *ops)
{
	ipi = ops;
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
	ipi->clear(h->hartid);
	atomic_store_ulong(&h->ipi_pending, 0);
	csr_set(mie, MIP_MSIP);
}

void ipi_kick(unsigned long hartid)
{
	if (ipi)
		ipi->send(hartid);
}

void ipi_send(unsigned long hartid, enum ipi_event event)
{
	struct hart *h = hart_get(hartid);

	if (!ipi || !h)
		return;
	atomic_or_ulong(&h->ipi_pending, BIT(event));
	ipi->send(hartid);
}

void ipi_send_mask(const struct hartmask *mask, enum ipi_event event)
{
	unsigned long hartid = 0;

	for_each_hart_in_mask(hartid, mask)
		ipi_send(hartid, event);
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
	ipi->clear(h->hartid);
	pending = atomic_swap_ulong(&h->ipi_pending, 0);

	if (pending & BIT(IPI_EVENT_HALT))
		hart_halt();
	if (pending & BIT(IPI_EVENT_RFENCE))
		rfence_process();
	if (pending & BIT(IPI_EVENT_SMODE))
		csr_set(mip, MIP_SSIP);
}
