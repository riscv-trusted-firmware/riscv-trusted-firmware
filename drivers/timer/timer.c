// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <timer.h>
#include <util.h>

static const struct timer_ops *timer;

void timer_register(const struct timer_ops *ops)
{
	timer = ops;
}

bool timer_available(void)
{
	return timer;
}

const char *timer_name(void)
{
	return timer ? timer->name : "none";
}

uint64_t timer_now(void)
{
	return timer ? timer->now() : 0;
}

void timer_hart_init(void)
{
	csr_clear(mie, MIP_MTIP);
	if (timer)
		timer->stop_event();
}

void timer_smode_set(uint64_t when)
{
	if (hart_has(HART_FEAT_SSTC)) {
#if __RISCV_XLEN__ == 32
		/* No spurious match while the two halves are inconsistent. */
		csr_write(CSR_STIMECMP, ~UL(0));
		csr_write(CSR_STIMECMPH, (unsigned long)(when >> 32));
#endif
		csr_write(CSR_STIMECMP, (unsigned long)when);
		return;
	}

	if (!timer)
		return;
	/* A new deadline retires the pending interrupt of the previous one. */
	csr_clear(mip, MIP_STIP);
	timer->set_event(when);
	csr_set(mie, MIP_MTIP);
}

void timer_process(void)
{
	/* MTIP stays asserted until S-mode sets a new deadline: mask it. */
	csr_clear(mie, MIP_MTIP);
	csr_set(mip, MIP_STIP);
}
