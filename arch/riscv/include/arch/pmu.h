/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_PMU_H
#define ARCH_PMU_H

/*
 * Performance monitoring: the machine counters (mcycle, minstret,
 * mhpmcounter3-31) handed out to S-mode, and firmware counters that count
 * what the monitor does on S-mode's behalf. Semantics and error codes are
 * those of the SBI PMU extension.
 *
 * Counter indices: 0-31 are the hardware counters, by CSR offset (1, the
 * place of 'time', is not a counter); PMU_FW_FIRST and up the firmware ones.
 */

#include <stdint.h>

#define PMU_HW_COUNTERS 32
#define PMU_FW_COUNTERS 16
#define PMU_FW_FIRST PMU_HW_COUNTERS
#define PMU_COUNTERS (PMU_HW_COUNTERS + PMU_FW_COUNTERS)

#ifdef CONFIG_SBI_PMU

/* Boot hart, once: probe the counters, read the "riscv,pmu" node if any. */
void pmu_init(const void *fdt);
/* Every hart, before it enters the next stage: all counters released. */
void pmu_hart_init(void);

/* Count one occurrence of firmware event SBI_PMU_FW_* on the calling hart. */
void pmu_fw_event(unsigned int event);

long pmu_counter_info(unsigned long idx, unsigned long *info);
long pmu_counter_config(unsigned long base, unsigned long mask,
			unsigned long flags, unsigned long event_idx,
			uint64_t event_data, unsigned long *idx);
long pmu_counter_start(unsigned long base, unsigned long mask,
		       unsigned long flags, uint64_t value);
long pmu_counter_stop(unsigned long base, unsigned long mask,
		      unsigned long flags);
long pmu_counter_fw_read(unsigned long idx, uint64_t *value);
/* Fill in the 'supported' bit of 'count' event entries at physical 'addr'. */
long pmu_event_info(unsigned long addr, unsigned long count);

#else

static inline void pmu_init(const void *fdt)
{
}

static inline void pmu_hart_init(void)
{
}

static inline void pmu_fw_event(unsigned int event)
{
}

#endif

#endif
