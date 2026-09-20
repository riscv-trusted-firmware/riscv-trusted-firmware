// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <arch/hart.h>
#include <arch/pmu.h>
#include <fdt_util.h>
#include <sbi/sbi.h>
#include <timer.h>
#include <util.h>

static const struct timer_ops *timer;

static uint64_t frequency = CONFIG_PLATFORM_TIMEBASE_FREQUENCY;

void timer_frequency_from_fdt(const void *fdt)
{
	int cpus = fdt_path_offset(fdt, "/cpus");
	uint32_t hz =
		cpus < 0 ? 0 : fdt_prop_u32(fdt, cpus, "timebase-frequency", 0);

	if (hz)
		frequency = hz;
}

uint64_t timer_frequency(void)
{
	return frequency;
}

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

void timer_udelay(uint64_t us)
{
	uint64_t until = timer_now() + timer_usecs_to_ticks(us);

	while (timer && timer_now() < until)
		cpu_relax();
}

/* Without Sstc: what S-mode asked for last, for whoever wants it back. */
static uint64_t deadline[CONFIG_PLATFORM_HART_COUNT];

void timer_hart_init(void)
{
	deadline[this_hart_index()] = ~ULL(0);
	csr_clear(mie, MIP_MTIP);
	if (timer)
		timer->stop_event();
}

uint64_t timer_smode_get(void)
{
	if (!hart_has(HART_FEAT_SSTC))
		return deadline[this_hart_index()];
#if __RISCV_XLEN__ == 32
	return reg_pair_to_64(csr_read(CSR_STIMECMPH), csr_read(CSR_STIMECMP));
#else
	return csr_read(CSR_STIMECMP);
#endif
}

void timer_smode_set(uint64_t when)
{
	pmu_fw_event(SBI_PMU_FW_SET_TIMER);
	timer_smode_restore(when);
}

void timer_smode_restore(uint64_t when)
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
	deadline[this_hart_index()] = when;
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
