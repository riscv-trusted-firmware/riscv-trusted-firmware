// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Sources of the SSE RAS events, see <arch/ras.h>.
 */

#include <arch/csr.h>
#include <arch/hart.h>
#include <arch/ras.h>
#include <arch/sse.h>
#include <atomic.h>
#include <domain.h>
#include <util.h>

static const uint32_t event_ids[RAS_KINDS] = {
	[RAS_LOCAL_HIGH] = SSE_EVENT_LOCAL_HIGH_RAS,
	[RAS_GLOBAL_HIGH] = SSE_EVENT_GLOBAL_HIGH_RAS,
	[RAS_LOCAL_LOW] = SSE_EVENT_LOCAL_LOW_RAS,
	[RAS_GLOBAL_LOW] = SSE_EVENT_GLOBAL_LOW_RAS,
};

/* Bit per kind the platform has a source of. */
static unsigned long sources;

/* The interrupts are above 31: mieh has them on RV32. */
#if __RISCV_XLEN__ == 32
#define RAS_IE_CSR CSR_MIEH
#define RAS_IE_BIT(irq) BIT((irq) - 32)
#else
#define RAS_IE_CSR mie
#define RAS_IE_BIT(irq) BIT(irq)
#endif

static int kind_of(uint32_t event_id)
{
	for (int kind = 0; kind < RAS_KINDS; kind++)
		if (event_ids[kind] == event_id)
			return kind;
	return -1;
}

/* 0: the event is not a local one. */
static unsigned int irq_of(uint32_t event_id)
{
	if (event_id == SSE_EVENT_LOCAL_HIGH_RAS)
		return IRQ_RAS_HIGH;
	return event_id == SSE_EVENT_LOCAL_LOW_RAS ? IRQ_RAS_LOW : 0;
}

bool ras_irqs_probe(void)
{
	unsigned long bits = RAS_IE_BIT(IRQ_RAS_LOW) | RAS_IE_BIT(IRQ_RAS_HIGH);
	unsigned long val = 0, got = 0;

	/*
	 * mieh comes with Smaia. Nothing is enabled for longer than this takes.
	 */
	if (!csr_probe(RAS_IE_CSR, &val))
		return false;
	csr_set(RAS_IE_CSR, bits);
	got = csr_read(RAS_IE_CSR) & bits;
	csr_write(RAS_IE_CSR, val & ~bits);
	return got == bits;
}

bool ras_is_event(uint32_t event_id)
{
	return kind_of(event_id) >= 0;
}

bool ras_event_available(uint32_t event_id)
{
	int kind = kind_of(event_id);

	if (kind < 0)
		return false;
	if (atomic_load_ulong(&sources) & BIT(kind))
		return true;
	return irq_of(event_id) && hart_has(HART_FEAT_RAS_IRQ);
}

void ras_event_arm(uint32_t event_id, bool armed)
{
	unsigned int irq = irq_of(event_id);

	if (!irq || !hart_has(HART_FEAT_RAS_IRQ))
		return;
	if (armed)
		csr_set(RAS_IE_CSR, RAS_IE_BIT(irq));
	else
		csr_clear(RAS_IE_CSR, RAS_IE_BIT(irq));
}

bool ras_irq(unsigned long irq)
{
	uint32_t event_id = 0;

	if (irq == IRQ_RAS_HIGH)
		event_id = SSE_EVENT_LOCAL_HIGH_RAS;
	else if (irq == IRQ_RAS_LOW)
		event_id = SSE_EVENT_LOCAL_LOW_RAS;
	else
		return false;

	/*
	 * Still up when S-mode returns: off until the handler has completed,
	 * which arms an event that stays enabled again.
	 */
	csr_clear(RAS_IE_CSR, RAS_IE_BIT(irq));
	sse_raise_local(event_id);
	return true;
}

void ras_source_register(enum ras_kind kind)
{
	atomic_or_ulong(&sources, BIT(kind));
}

void ras_report(enum ras_kind kind)
{
	if (!(atomic_load_ulong(&sources) & BIT(kind)))
		return;
	if (kind == RAS_LOCAL_HIGH || kind == RAS_LOCAL_LOW)
		sse_raise_local(event_ids[kind]);
	else
		/*
		 * Every domain that asked: nothing says whose the hardware at
		 * fault is.
		 */
		for (unsigned int key = 0; key < domain_keys(); key++)
			sse_raise_global(event_ids[kind], key);
}
