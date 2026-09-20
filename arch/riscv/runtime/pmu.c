// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Performance monitoring unit, see <arch/pmu.h>.
 *
 * Which event a programmable counter can count, and the mhpmevent value
 * that selects it, is platform knowledge. It comes from the "riscv,pmu"
 * device tree node (Linux: Documentation/devicetree/bindings/perf/
 * riscv,pmu.yaml). Without one, only cycles and instructions are offered,
 * on their fixed counters, next to the firmware counters.
 */

#include <arch/hart.h>
#include <arch/pmp.h>
#include <arch/pmu.h>
#include <domain.h>
#include <libfdt.h>
#include <log.h>
#include <sbi/sbi.h>
#include <util.h>

#define EVENT_NONE (~U(0))
#define EVENT_TYPE(idx) (((idx) >> 16) & 0xf)
#define EVENT_CODE(idx) ((idx) & 0xffff)
/* Raw events: the low 48 bits of event_data are the select value. */
#define RAW_EVENT_MASK ULL(0xffffffffffff)

#define MAX_EVENT_RANGES 32
#define MAX_EVENT_SELECTS 64
#define MAX_RAW_EVENTS 16

/* <first event_idx, last event_idx, bitmap of counters that can count them> */
struct event_range {
	uint32_t first, last, counters;
};

/* event_idx -> mhpmevent value, when it is not the event_idx itself */
struct event_select {
	uint32_t event_idx;
	uint64_t select;
};

struct raw_event {
	uint64_t select, mask;
	uint32_t counters;
};

static struct event_range ranges[MAX_EVENT_RANGES];
static struct event_select selects[MAX_EVENT_SELECTS];
static struct raw_event raw_events[MAX_RAW_EVENTS];
static unsigned int nr_ranges, nr_selects, nr_raw_events;

/* Hardware counters that exist (bit 1, 'time', never does) and their width. */
static uint32_t hw_counters;
static bool have_inhibit;

/* mcycle, minstret and mhpmcounterN are 64 bits wide on every XLEN. */
#define HW_COUNTER_WIDTH 64

#define SNAPSHOT_NONE (~UL(0))
#define SNAPSHOT_SIZE UL(4096)
#define SNAPSHOT_VALUES 64

/* Snapshot shared memory: indices are relative to a call's counter_idx_base. */
struct pmu_snapshot {
	uint64_t overflow_bitmap;
	uint64_t values[SNAPSHOT_VALUES];
};

struct pmu_hart {
	/* physical address, or SNAPSHOT_NONE */
	unsigned long snapshot;
	/* overflows go to the SSE event */
	bool sse_enabled;
	bool sse_saved_ie;
	uint32_t event[PMU_COUNTERS]; /* EVENT_NONE: free */
	uint32_t fw_started; /* bit per firmware counter */
	uint64_t fw_value[PMU_FW_COUNTERS];
	/*
	 * While the hart runs another domain: the hardware side of the above.
	 */
	struct {
		unsigned long inhibit;
		unsigned long irq_enabled, irq_delegated, irq_pending;
		uint64_t value[PMU_HW_COUNTERS];
		uint64_t select[PMU_HW_COUNTERS];
	} hw;
};

/* Per domain and hart, see <domain.h>. */
static struct pmu_hart pmu_harts[DOMAIN_KEYS][CONFIG_PLATFORM_HART_COUNT];

/* Without Zbb the compiler builtins for these are library calls. */
static unsigned int count_bits(uint32_t v)
{
	unsigned int n = 0;

	for (; v; v &= v - 1)
		n++;
	return n;
}

static unsigned int lowest_bit(unsigned long v)
{
	unsigned int n = 0;

	while (!(v & 1)) {
		v >>= 1;
		n++;
	}
	return n;
}

static struct pmu_hart *this_pmu(void)
{
	return &pmu_harts[this_domain_key()][this_hart_index()];
}

/* ---- CSR access by counter number -------------------------------------- */

/*
 * mhpmcounter3-31 / mhpmevent3-31; mcycle and minstret sit at offsets 0 and 2.
 */
/*
 * mhpmcounter3-31 / mhpmevent3-31 by number; mcycle and minstret sit at
 * offsets 0 and 2. A CSR number has to be an immediate: one case each.
 */
static unsigned long mcounter_read(unsigned int n)
{
	unsigned long val = 0;

	switch (n) {
	case 0:
		val = csr_read(CSR_MCYCLE);
		break;
	case 2:
		val = csr_read(CSR_MCYCLE + 2);
		break;
	case 3:
		val = csr_read(CSR_MCYCLE + 3);
		break;
	case 4:
		val = csr_read(CSR_MCYCLE + 4);
		break;
	case 5:
		val = csr_read(CSR_MCYCLE + 5);
		break;
	case 6:
		val = csr_read(CSR_MCYCLE + 6);
		break;
	case 7:
		val = csr_read(CSR_MCYCLE + 7);
		break;
	case 8:
		val = csr_read(CSR_MCYCLE + 8);
		break;
	case 9:
		val = csr_read(CSR_MCYCLE + 9);
		break;
	case 10:
		val = csr_read(CSR_MCYCLE + 10);
		break;
	case 11:
		val = csr_read(CSR_MCYCLE + 11);
		break;
	case 12:
		val = csr_read(CSR_MCYCLE + 12);
		break;
	case 13:
		val = csr_read(CSR_MCYCLE + 13);
		break;
	case 14:
		val = csr_read(CSR_MCYCLE + 14);
		break;
	case 15:
		val = csr_read(CSR_MCYCLE + 15);
		break;
	case 16:
		val = csr_read(CSR_MCYCLE + 16);
		break;
	case 17:
		val = csr_read(CSR_MCYCLE + 17);
		break;
	case 18:
		val = csr_read(CSR_MCYCLE + 18);
		break;
	case 19:
		val = csr_read(CSR_MCYCLE + 19);
		break;
	case 20:
		val = csr_read(CSR_MCYCLE + 20);
		break;
	case 21:
		val = csr_read(CSR_MCYCLE + 21);
		break;
	case 22:
		val = csr_read(CSR_MCYCLE + 22);
		break;
	case 23:
		val = csr_read(CSR_MCYCLE + 23);
		break;
	case 24:
		val = csr_read(CSR_MCYCLE + 24);
		break;
	case 25:
		val = csr_read(CSR_MCYCLE + 25);
		break;
	case 26:
		val = csr_read(CSR_MCYCLE + 26);
		break;
	case 27:
		val = csr_read(CSR_MCYCLE + 27);
		break;
	case 28:
		val = csr_read(CSR_MCYCLE + 28);
		break;
	case 29:
		val = csr_read(CSR_MCYCLE + 29);
		break;
	case 30:
		val = csr_read(CSR_MCYCLE + 30);
		break;
	case 31:
		val = csr_read(CSR_MCYCLE + 31);
		break;
	default:
		break;
	}
	return val;
}

static void mcounter_write(unsigned int n, unsigned long val)
{
	switch (n) {
	case 0:
		csr_write(CSR_MCYCLE, val);
		break;
	case 2:
		csr_write(CSR_MCYCLE + 2, val);
		break;
	case 3:
		csr_write(CSR_MCYCLE + 3, val);
		break;
	case 4:
		csr_write(CSR_MCYCLE + 4, val);
		break;
	case 5:
		csr_write(CSR_MCYCLE + 5, val);
		break;
	case 6:
		csr_write(CSR_MCYCLE + 6, val);
		break;
	case 7:
		csr_write(CSR_MCYCLE + 7, val);
		break;
	case 8:
		csr_write(CSR_MCYCLE + 8, val);
		break;
	case 9:
		csr_write(CSR_MCYCLE + 9, val);
		break;
	case 10:
		csr_write(CSR_MCYCLE + 10, val);
		break;
	case 11:
		csr_write(CSR_MCYCLE + 11, val);
		break;
	case 12:
		csr_write(CSR_MCYCLE + 12, val);
		break;
	case 13:
		csr_write(CSR_MCYCLE + 13, val);
		break;
	case 14:
		csr_write(CSR_MCYCLE + 14, val);
		break;
	case 15:
		csr_write(CSR_MCYCLE + 15, val);
		break;
	case 16:
		csr_write(CSR_MCYCLE + 16, val);
		break;
	case 17:
		csr_write(CSR_MCYCLE + 17, val);
		break;
	case 18:
		csr_write(CSR_MCYCLE + 18, val);
		break;
	case 19:
		csr_write(CSR_MCYCLE + 19, val);
		break;
	case 20:
		csr_write(CSR_MCYCLE + 20, val);
		break;
	case 21:
		csr_write(CSR_MCYCLE + 21, val);
		break;
	case 22:
		csr_write(CSR_MCYCLE + 22, val);
		break;
	case 23:
		csr_write(CSR_MCYCLE + 23, val);
		break;
	case 24:
		csr_write(CSR_MCYCLE + 24, val);
		break;
	case 25:
		csr_write(CSR_MCYCLE + 25, val);
		break;
	case 26:
		csr_write(CSR_MCYCLE + 26, val);
		break;
	case 27:
		csr_write(CSR_MCYCLE + 27, val);
		break;
	case 28:
		csr_write(CSR_MCYCLE + 28, val);
		break;
	case 29:
		csr_write(CSR_MCYCLE + 29, val);
		break;
	case 30:
		csr_write(CSR_MCYCLE + 30, val);
		break;
	case 31:
		csr_write(CSR_MCYCLE + 31, val);
		break;
	default:
		break;
	}
}

static unsigned long __unused mhpmevent_read(unsigned int n)
{
	unsigned long val = 0;

	switch (n) {
	case 3:
		val = csr_read(CSR_MHPMEVENT0 + 3);
		break;
	case 4:
		val = csr_read(CSR_MHPMEVENT0 + 4);
		break;
	case 5:
		val = csr_read(CSR_MHPMEVENT0 + 5);
		break;
	case 6:
		val = csr_read(CSR_MHPMEVENT0 + 6);
		break;
	case 7:
		val = csr_read(CSR_MHPMEVENT0 + 7);
		break;
	case 8:
		val = csr_read(CSR_MHPMEVENT0 + 8);
		break;
	case 9:
		val = csr_read(CSR_MHPMEVENT0 + 9);
		break;
	case 10:
		val = csr_read(CSR_MHPMEVENT0 + 10);
		break;
	case 11:
		val = csr_read(CSR_MHPMEVENT0 + 11);
		break;
	case 12:
		val = csr_read(CSR_MHPMEVENT0 + 12);
		break;
	case 13:
		val = csr_read(CSR_MHPMEVENT0 + 13);
		break;
	case 14:
		val = csr_read(CSR_MHPMEVENT0 + 14);
		break;
	case 15:
		val = csr_read(CSR_MHPMEVENT0 + 15);
		break;
	case 16:
		val = csr_read(CSR_MHPMEVENT0 + 16);
		break;
	case 17:
		val = csr_read(CSR_MHPMEVENT0 + 17);
		break;
	case 18:
		val = csr_read(CSR_MHPMEVENT0 + 18);
		break;
	case 19:
		val = csr_read(CSR_MHPMEVENT0 + 19);
		break;
	case 20:
		val = csr_read(CSR_MHPMEVENT0 + 20);
		break;
	case 21:
		val = csr_read(CSR_MHPMEVENT0 + 21);
		break;
	case 22:
		val = csr_read(CSR_MHPMEVENT0 + 22);
		break;
	case 23:
		val = csr_read(CSR_MHPMEVENT0 + 23);
		break;
	case 24:
		val = csr_read(CSR_MHPMEVENT0 + 24);
		break;
	case 25:
		val = csr_read(CSR_MHPMEVENT0 + 25);
		break;
	case 26:
		val = csr_read(CSR_MHPMEVENT0 + 26);
		break;
	case 27:
		val = csr_read(CSR_MHPMEVENT0 + 27);
		break;
	case 28:
		val = csr_read(CSR_MHPMEVENT0 + 28);
		break;
	case 29:
		val = csr_read(CSR_MHPMEVENT0 + 29);
		break;
	case 30:
		val = csr_read(CSR_MHPMEVENT0 + 30);
		break;
	case 31:
		val = csr_read(CSR_MHPMEVENT0 + 31);
		break;
	default:
		break;
	}
	return val;
}

static void __unused mhpmevent_write(unsigned int n, unsigned long val)
{
	switch (n) {
	case 3:
		csr_write(CSR_MHPMEVENT0 + 3, val);
		break;
	case 4:
		csr_write(CSR_MHPMEVENT0 + 4, val);
		break;
	case 5:
		csr_write(CSR_MHPMEVENT0 + 5, val);
		break;
	case 6:
		csr_write(CSR_MHPMEVENT0 + 6, val);
		break;
	case 7:
		csr_write(CSR_MHPMEVENT0 + 7, val);
		break;
	case 8:
		csr_write(CSR_MHPMEVENT0 + 8, val);
		break;
	case 9:
		csr_write(CSR_MHPMEVENT0 + 9, val);
		break;
	case 10:
		csr_write(CSR_MHPMEVENT0 + 10, val);
		break;
	case 11:
		csr_write(CSR_MHPMEVENT0 + 11, val);
		break;
	case 12:
		csr_write(CSR_MHPMEVENT0 + 12, val);
		break;
	case 13:
		csr_write(CSR_MHPMEVENT0 + 13, val);
		break;
	case 14:
		csr_write(CSR_MHPMEVENT0 + 14, val);
		break;
	case 15:
		csr_write(CSR_MHPMEVENT0 + 15, val);
		break;
	case 16:
		csr_write(CSR_MHPMEVENT0 + 16, val);
		break;
	case 17:
		csr_write(CSR_MHPMEVENT0 + 17, val);
		break;
	case 18:
		csr_write(CSR_MHPMEVENT0 + 18, val);
		break;
	case 19:
		csr_write(CSR_MHPMEVENT0 + 19, val);
		break;
	case 20:
		csr_write(CSR_MHPMEVENT0 + 20, val);
		break;
	case 21:
		csr_write(CSR_MHPMEVENT0 + 21, val);
		break;
	case 22:
		csr_write(CSR_MHPMEVENT0 + 22, val);
		break;
	case 23:
		csr_write(CSR_MHPMEVENT0 + 23, val);
		break;
	case 24:
		csr_write(CSR_MHPMEVENT0 + 24, val);
		break;
	case 25:
		csr_write(CSR_MHPMEVENT0 + 25, val);
		break;
	case 26:
		csr_write(CSR_MHPMEVENT0 + 26, val);
		break;
	case 27:
		csr_write(CSR_MHPMEVENT0 + 27, val);
		break;
	case 28:
		csr_write(CSR_MHPMEVENT0 + 28, val);
		break;
	case 29:
		csr_write(CSR_MHPMEVENT0 + 29, val);
		break;
	case 30:
		csr_write(CSR_MHPMEVENT0 + 30, val);
		break;
	case 31:
		csr_write(CSR_MHPMEVENT0 + 31, val);
		break;
	default:
		break;
	}
}

#if __RISCV_XLEN__ == 32
static unsigned long mcounterh_read(unsigned int n)
{
	unsigned long val = 0;

	switch (n) {
	case 0:
		val = csr_read(CSR_MCYCLEH);
		break;
	case 2:
		val = csr_read(CSR_MCYCLEH + 2);
		break;
	case 3:
		val = csr_read(CSR_MCYCLEH + 3);
		break;
	case 4:
		val = csr_read(CSR_MCYCLEH + 4);
		break;
	case 5:
		val = csr_read(CSR_MCYCLEH + 5);
		break;
	case 6:
		val = csr_read(CSR_MCYCLEH + 6);
		break;
	case 7:
		val = csr_read(CSR_MCYCLEH + 7);
		break;
	case 8:
		val = csr_read(CSR_MCYCLEH + 8);
		break;
	case 9:
		val = csr_read(CSR_MCYCLEH + 9);
		break;
	case 10:
		val = csr_read(CSR_MCYCLEH + 10);
		break;
	case 11:
		val = csr_read(CSR_MCYCLEH + 11);
		break;
	case 12:
		val = csr_read(CSR_MCYCLEH + 12);
		break;
	case 13:
		val = csr_read(CSR_MCYCLEH + 13);
		break;
	case 14:
		val = csr_read(CSR_MCYCLEH + 14);
		break;
	case 15:
		val = csr_read(CSR_MCYCLEH + 15);
		break;
	case 16:
		val = csr_read(CSR_MCYCLEH + 16);
		break;
	case 17:
		val = csr_read(CSR_MCYCLEH + 17);
		break;
	case 18:
		val = csr_read(CSR_MCYCLEH + 18);
		break;
	case 19:
		val = csr_read(CSR_MCYCLEH + 19);
		break;
	case 20:
		val = csr_read(CSR_MCYCLEH + 20);
		break;
	case 21:
		val = csr_read(CSR_MCYCLEH + 21);
		break;
	case 22:
		val = csr_read(CSR_MCYCLEH + 22);
		break;
	case 23:
		val = csr_read(CSR_MCYCLEH + 23);
		break;
	case 24:
		val = csr_read(CSR_MCYCLEH + 24);
		break;
	case 25:
		val = csr_read(CSR_MCYCLEH + 25);
		break;
	case 26:
		val = csr_read(CSR_MCYCLEH + 26);
		break;
	case 27:
		val = csr_read(CSR_MCYCLEH + 27);
		break;
	case 28:
		val = csr_read(CSR_MCYCLEH + 28);
		break;
	case 29:
		val = csr_read(CSR_MCYCLEH + 29);
		break;
	case 30:
		val = csr_read(CSR_MCYCLEH + 30);
		break;
	case 31:
		val = csr_read(CSR_MCYCLEH + 31);
		break;
	default:
		break;
	}
	return val;
}

static void mcounterh_write(unsigned int n, unsigned long val)
{
	switch (n) {
	case 0:
		csr_write(CSR_MCYCLEH, val);
		break;
	case 2:
		csr_write(CSR_MCYCLEH + 2, val);
		break;
	case 3:
		csr_write(CSR_MCYCLEH + 3, val);
		break;
	case 4:
		csr_write(CSR_MCYCLEH + 4, val);
		break;
	case 5:
		csr_write(CSR_MCYCLEH + 5, val);
		break;
	case 6:
		csr_write(CSR_MCYCLEH + 6, val);
		break;
	case 7:
		csr_write(CSR_MCYCLEH + 7, val);
		break;
	case 8:
		csr_write(CSR_MCYCLEH + 8, val);
		break;
	case 9:
		csr_write(CSR_MCYCLEH + 9, val);
		break;
	case 10:
		csr_write(CSR_MCYCLEH + 10, val);
		break;
	case 11:
		csr_write(CSR_MCYCLEH + 11, val);
		break;
	case 12:
		csr_write(CSR_MCYCLEH + 12, val);
		break;
	case 13:
		csr_write(CSR_MCYCLEH + 13, val);
		break;
	case 14:
		csr_write(CSR_MCYCLEH + 14, val);
		break;
	case 15:
		csr_write(CSR_MCYCLEH + 15, val);
		break;
	case 16:
		csr_write(CSR_MCYCLEH + 16, val);
		break;
	case 17:
		csr_write(CSR_MCYCLEH + 17, val);
		break;
	case 18:
		csr_write(CSR_MCYCLEH + 18, val);
		break;
	case 19:
		csr_write(CSR_MCYCLEH + 19, val);
		break;
	case 20:
		csr_write(CSR_MCYCLEH + 20, val);
		break;
	case 21:
		csr_write(CSR_MCYCLEH + 21, val);
		break;
	case 22:
		csr_write(CSR_MCYCLEH + 22, val);
		break;
	case 23:
		csr_write(CSR_MCYCLEH + 23, val);
		break;
	case 24:
		csr_write(CSR_MCYCLEH + 24, val);
		break;
	case 25:
		csr_write(CSR_MCYCLEH + 25, val);
		break;
	case 26:
		csr_write(CSR_MCYCLEH + 26, val);
		break;
	case 27:
		csr_write(CSR_MCYCLEH + 27, val);
		break;
	case 28:
		csr_write(CSR_MCYCLEH + 28, val);
		break;
	case 29:
		csr_write(CSR_MCYCLEH + 29, val);
		break;
	case 30:
		csr_write(CSR_MCYCLEH + 30, val);
		break;
	case 31:
		csr_write(CSR_MCYCLEH + 31, val);
		break;
	default:
		break;
	}
}

static unsigned long __unused mhpmeventh_read(unsigned int n)
{
	unsigned long val = 0;

	switch (n) {
	case 3:
		val = csr_read(CSR_MHPMEVENT0H + 3);
		break;
	case 4:
		val = csr_read(CSR_MHPMEVENT0H + 4);
		break;
	case 5:
		val = csr_read(CSR_MHPMEVENT0H + 5);
		break;
	case 6:
		val = csr_read(CSR_MHPMEVENT0H + 6);
		break;
	case 7:
		val = csr_read(CSR_MHPMEVENT0H + 7);
		break;
	case 8:
		val = csr_read(CSR_MHPMEVENT0H + 8);
		break;
	case 9:
		val = csr_read(CSR_MHPMEVENT0H + 9);
		break;
	case 10:
		val = csr_read(CSR_MHPMEVENT0H + 10);
		break;
	case 11:
		val = csr_read(CSR_MHPMEVENT0H + 11);
		break;
	case 12:
		val = csr_read(CSR_MHPMEVENT0H + 12);
		break;
	case 13:
		val = csr_read(CSR_MHPMEVENT0H + 13);
		break;
	case 14:
		val = csr_read(CSR_MHPMEVENT0H + 14);
		break;
	case 15:
		val = csr_read(CSR_MHPMEVENT0H + 15);
		break;
	case 16:
		val = csr_read(CSR_MHPMEVENT0H + 16);
		break;
	case 17:
		val = csr_read(CSR_MHPMEVENT0H + 17);
		break;
	case 18:
		val = csr_read(CSR_MHPMEVENT0H + 18);
		break;
	case 19:
		val = csr_read(CSR_MHPMEVENT0H + 19);
		break;
	case 20:
		val = csr_read(CSR_MHPMEVENT0H + 20);
		break;
	case 21:
		val = csr_read(CSR_MHPMEVENT0H + 21);
		break;
	case 22:
		val = csr_read(CSR_MHPMEVENT0H + 22);
		break;
	case 23:
		val = csr_read(CSR_MHPMEVENT0H + 23);
		break;
	case 24:
		val = csr_read(CSR_MHPMEVENT0H + 24);
		break;
	case 25:
		val = csr_read(CSR_MHPMEVENT0H + 25);
		break;
	case 26:
		val = csr_read(CSR_MHPMEVENT0H + 26);
		break;
	case 27:
		val = csr_read(CSR_MHPMEVENT0H + 27);
		break;
	case 28:
		val = csr_read(CSR_MHPMEVENT0H + 28);
		break;
	case 29:
		val = csr_read(CSR_MHPMEVENT0H + 29);
		break;
	case 30:
		val = csr_read(CSR_MHPMEVENT0H + 30);
		break;
	case 31:
		val = csr_read(CSR_MHPMEVENT0H + 31);
		break;
	default:
		break;
	}
	return val;
}

static void __unused mhpmeventh_write(unsigned int n, unsigned long val)
{
	switch (n) {
	case 3:
		csr_write(CSR_MHPMEVENT0H + 3, val);
		break;
	case 4:
		csr_write(CSR_MHPMEVENT0H + 4, val);
		break;
	case 5:
		csr_write(CSR_MHPMEVENT0H + 5, val);
		break;
	case 6:
		csr_write(CSR_MHPMEVENT0H + 6, val);
		break;
	case 7:
		csr_write(CSR_MHPMEVENT0H + 7, val);
		break;
	case 8:
		csr_write(CSR_MHPMEVENT0H + 8, val);
		break;
	case 9:
		csr_write(CSR_MHPMEVENT0H + 9, val);
		break;
	case 10:
		csr_write(CSR_MHPMEVENT0H + 10, val);
		break;
	case 11:
		csr_write(CSR_MHPMEVENT0H + 11, val);
		break;
	case 12:
		csr_write(CSR_MHPMEVENT0H + 12, val);
		break;
	case 13:
		csr_write(CSR_MHPMEVENT0H + 13, val);
		break;
	case 14:
		csr_write(CSR_MHPMEVENT0H + 14, val);
		break;
	case 15:
		csr_write(CSR_MHPMEVENT0H + 15, val);
		break;
	case 16:
		csr_write(CSR_MHPMEVENT0H + 16, val);
		break;
	case 17:
		csr_write(CSR_MHPMEVENT0H + 17, val);
		break;
	case 18:
		csr_write(CSR_MHPMEVENT0H + 18, val);
		break;
	case 19:
		csr_write(CSR_MHPMEVENT0H + 19, val);
		break;
	case 20:
		csr_write(CSR_MHPMEVENT0H + 20, val);
		break;
	case 21:
		csr_write(CSR_MHPMEVENT0H + 21, val);
		break;
	case 22:
		csr_write(CSR_MHPMEVENT0H + 22, val);
		break;
	case 23:
		csr_write(CSR_MHPMEVENT0H + 23, val);
		break;
	case 24:
		csr_write(CSR_MHPMEVENT0H + 24, val);
		break;
	case 25:
		csr_write(CSR_MHPMEVENT0H + 25, val);
		break;
	case 26:
		csr_write(CSR_MHPMEVENT0H + 26, val);
		break;
	case 27:
		csr_write(CSR_MHPMEVENT0H + 27, val);
		break;
	case 28:
		csr_write(CSR_MHPMEVENT0H + 28, val);
		break;
	case 29:
		csr_write(CSR_MHPMEVENT0H + 29, val);
		break;
	case 30:
		csr_write(CSR_MHPMEVENT0H + 30, val);
		break;
	case 31:
		csr_write(CSR_MHPMEVENT0H + 31, val);
		break;
	default:
		break;
	}
}

#endif

static void hw_counter_write(unsigned int n, uint64_t val)
{
	mcounter_write(n, (unsigned long)val);
#if __RISCV_XLEN__ == 32
	mcounterh_write(n, (unsigned long)(val >> 32));
#endif
}

static uint64_t hw_counter_read(unsigned int n)
{
#if __RISCV_XLEN__ == 32
	unsigned long hi = 0, lo = 0;

	do {
		hi = mcounterh_read(n);
		lo = mcounter_read(n);
	} while (hi != mcounterh_read(n));
	return reg_pair_to_64(hi, lo);
#else
	return mcounter_read(n);
#endif
}

static uint64_t hw_event_read(unsigned int n)
{
	uint64_t val = mhpmevent_read(n);

#if __RISCV_XLEN__ == 32
	if (hart_has(HART_FEAT_SSCOFPMF))
		val |= SHIFT_U64(mhpmeventh_read(n), 32);
#endif
	return val;
}

static void hw_event_write(unsigned int n, uint64_t val)
{
	if (n < 3)
		return;
	mhpmevent_write(n, (unsigned long)val);
#if __RISCV_XLEN__ == 32
	/* The upper half only exists with Sscofpmf. */
	if (hart_has(HART_FEAT_SSCOFPMF))
		mhpmeventh_write(n, (unsigned long)(val >> 32));
#endif
}

static bool hw_counter_running(unsigned int n)
{
	return !(csr_read(CSR_MCOUNTINHIBIT) & BIT(n));
}

/* ---- boot-time discovery ----------------------------------------------- */

static void add_range(uint32_t first, uint32_t last, uint32_t counters)
{
	if (nr_ranges == MAX_EVENT_RANGES || first > last)
		return;
	ranges[nr_ranges++] = (struct event_range){ first, last, counters };
}

static void pmu_parse_fdt(const void *fdt)
{
	const fdt32_t *p = NULL;
	int node = 0, len = 0;

	if (!fdt || fdt_check_header(fdt))
		return;
	node = fdt_node_offset_by_compatible(fdt, -1, "riscv,pmu");
	if (node < 0)
		return;

	p = fdt_getprop(fdt, node, "riscv,event-to-mhpmcounters", &len);
	for (; p && len >= 12; p += 3, len -= 12)
		add_range(fdt32_to_cpu(p[0]), fdt32_to_cpu(p[1]),
			  fdt32_to_cpu(p[2]));

	p = fdt_getprop(fdt, node, "riscv,event-to-mhpmevent", &len);
	for (; p && len >= 12 && nr_selects < MAX_EVENT_SELECTS;
	     p += 3, len -= 12)
		selects[nr_selects++] = (struct event_select){
			.event_idx = fdt32_to_cpu(p[0]),
			.select = reg_pair_to_64(fdt32_to_cpu(p[1]),
						 fdt32_to_cpu(p[2])),
		};

	p = fdt_getprop(fdt, node, "riscv,raw-event-to-mhpmcounters", &len);
	for (; p && len >= 20 && nr_raw_events < MAX_RAW_EVENTS;
	     p += 5, len -= 20)
		raw_events[nr_raw_events++] = (struct raw_event){
			.select = reg_pair_to_64(fdt32_to_cpu(p[0]),
						 fdt32_to_cpu(p[1])),
			.mask = reg_pair_to_64(fdt32_to_cpu(p[2]),
					       fdt32_to_cpu(p[3])),
			.counters = fdt32_to_cpu(p[4]),
		};
}

void pmu_init(const void *fdt)
{
	bool writable = false;
	unsigned long val = 0;

	/* Counters can only be handed out if they can be stopped. */
	have_inhibit = csr_probe(CSR_MCOUNTINHIBIT, &val);
	if (have_inhibit) {
		hw_counters = BIT(0) | BIT(2);
		/*
		 * An unimplemented mhpmcounter is hardwired to zero; some
		 * implementations (QEMU) trap instead.
		 */
		for (unsigned int n = 3; n < PMU_HW_COUNTERS; n++) {
			val = 0;
			writable = may_trap(({
				mcounter_write(n, 1);
				val = mcounter_read(n);
				mcounter_write(n, 0);
			}));
			if (writable && val)
				hw_counters |= (uint32_t)BIT(n);
		}
	}

	pmu_parse_fdt(fdt);
	if (!nr_ranges) {
		add_range(SBI_PMU_HW_CPU_CYCLES, SBI_PMU_HW_CPU_CYCLES, BIT(0));
		add_range(SBI_PMU_HW_INSTRUCTIONS, SBI_PMU_HW_INSTRUCTIONS,
			  BIT(2));
	}

	pr_info("pmu: %u hardware counters (%u event ranges), %u firmware counters\n",
		count_bits(hw_counters), nr_ranges, PMU_FW_COUNTERS);
}

void pmu_hart_init(void)
{
	struct pmu_hart *p = this_pmu();

	for (unsigned int i = 0; i < PMU_COUNTERS; i++)
		p->event[i] = EVENT_NONE;
	p->fw_started = 0;
	p->snapshot = SNAPSHOT_NONE;
	p->sse_enabled = false;

	if (!have_inhibit)
		return;
	/* cycle and instret keep running for plain rdcycle/rdinstret users. */
	csr_write(CSR_MCOUNTINHIBIT, hw_counters & ~(BIT(0) | BIT(2)));
	for (unsigned int n = 3; n < PMU_HW_COUNTERS; n++)
		if (hw_counters & BIT(n))
			hw_event_write(n, 0);
}

/*
 * A domain counts what happens while it runs, cycles and instructions
 * included: the counters stand still for it while the hart is elsewhere,
 * and nothing of what another domain does shows in them.
 */
void pmu_hart_switch_out(void)
{
	struct pmu_hart *p = this_pmu();

	if (hart_has(HART_FEAT_SSCOFPMF)) {
		p->hw.irq_enabled = csr_read(mie) & BIT(IRQ_PMU_OVF);
		p->hw.irq_delegated = csr_read(mideleg) & BIT(IRQ_PMU_OVF);
		p->hw.irq_pending = csr_read(mip) & BIT(IRQ_PMU_OVF);
		csr_clear(mie, BIT(IRQ_PMU_OVF));
	}
	if (!have_inhibit)
		return;
	p->hw.inhibit = csr_read(CSR_MCOUNTINHIBIT);
	csr_write(CSR_MCOUNTINHIBIT, hw_counters);
	for (unsigned int n = 0; n < PMU_HW_COUNTERS; n++) {
		if (!(hw_counters & BIT(n)))
			continue;
		p->hw.value[n] = hw_counter_read(n);
		if (n >= 3)
			p->hw.select[n] = hw_event_read(n);
	}
}

void pmu_hart_switch_in(bool fresh)
{
	struct pmu_hart *p = this_pmu();

	if (fresh) {
		/* As pmu_hart_init() leaves it, with every counter at zero. */
		p->hw.inhibit = hw_counters & ~(BIT(0) | BIT(2));
		p->hw.irq_enabled = 0;
		p->hw.irq_delegated = BIT(IRQ_PMU_OVF);
		p->hw.irq_pending = 0;
		for (unsigned int n = 0; n < PMU_HW_COUNTERS; n++) {
			p->hw.value[n] = 0;
			p->hw.select[n] = 0;
		}
		pmu_hart_init();
	}
	if (have_inhibit) {
		csr_write(CSR_MCOUNTINHIBIT, hw_counters);
		for (unsigned int n = 0; n < PMU_HW_COUNTERS; n++) {
			if (!(hw_counters & BIT(n)))
				continue;
			hw_event_write(n, p->hw.select[n]);
			hw_counter_write(n, p->hw.value[n]);
		}
		csr_write(CSR_MCOUNTINHIBIT, p->hw.inhibit);
	}
	if (hart_has(HART_FEAT_SSCOFPMF)) {
		/* Whose interrupt it is first, then whether it is wanted. */
		csr_clear(mie, BIT(IRQ_PMU_OVF));
		csr_clear(mideleg, BIT(IRQ_PMU_OVF));
		csr_set(mideleg, p->hw.irq_delegated);
		csr_clear(mip, BIT(IRQ_PMU_OVF));
		csr_set(mip, p->hw.irq_pending);
		csr_set(mie, p->hw.irq_enabled);
	}
}

/* ---- firmware counters ------------------------------------------------- */

void pmu_fw_event(unsigned int event)
{
	struct pmu_hart *p = this_pmu();
	uint32_t idx = (SBI_PMU_EVENT_TYPE_FW << 16) | event;

	if (!p->fw_started)
		return;
	for (unsigned int i = 0; i < PMU_FW_COUNTERS; i++)
		if ((p->fw_started & BIT(i)) &&
		    p->event[PMU_FW_FIRST + i] == idx)
			p->fw_value[i]++;
}

/* ---- SBI semantics ----------------------------------------------------- */

static bool counter_exists(unsigned long idx)
{
	if (idx >= PMU_COUNTERS)
		return false;
	return idx >= PMU_FW_FIRST || (hw_counters & BIT(idx));
}

/* false when (base, mask) names a counter that does not exist, or none. */
static bool counter_set_ok(unsigned long base, unsigned long mask)
{
	if (!mask)
		return false;
	for (unsigned int i = 0; i < __RISCV_XLEN__; i++)
		if ((mask & BIT(i)) &&
		    (base + i < base || !counter_exists(base + i)))
			return false;
	return true;
}

long pmu_counter_info(unsigned long idx, unsigned long *info)
{
	if (!counter_exists(idx))
		return SBI_ERR_INVALID_PARAM;
	if (idx >= PMU_FW_FIRST)
		*info = BIT(__RISCV_XLEN__ - 1);
	else
		*info = (0xc00 + idx) | SHIFT_UL(HW_COUNTER_WIDTH - 1, 12);
	return SBI_SUCCESS;
}

/* Counters able to count the event, and what to write to mhpmevent for it. */
static uint32_t hw_event_lookup(uint32_t event_idx, uint64_t data,
				uint64_t *select)
{
	if (EVENT_TYPE(event_idx) == SBI_PMU_EVENT_TYPE_HW_RAW) {
		data &= RAW_EVENT_MASK;
		for (unsigned int i = 0; i < nr_raw_events; i++)
			if ((data & raw_events[i].mask) ==
			    raw_events[i].select) {
				*select = data;
				return raw_events[i].counters;
			}
		return 0;
	}

	*select = event_idx;
	for (unsigned int i = 0; i < nr_selects; i++)
		if (selects[i].event_idx == event_idx)
			*select = selects[i].select;
	for (unsigned int i = 0; i < nr_ranges; i++)
		if (event_idx >= ranges[i].first && event_idx <= ranges[i].last)
			return ranges[i].counters;
	return 0;
}

static uint64_t inhibit_bits(unsigned long flags)
{
	/* The monitor's own execution is never part of the measurement. */
	uint64_t bits = BIT64(HPMEVENT_MINH_BIT);

	if (flags & SBI_PMU_CFG_FLAG_SET_SINH)
		bits |= BIT64(HPMEVENT_SINH_BIT);
	if (flags & SBI_PMU_CFG_FLAG_SET_UINH)
		bits |= BIT64(HPMEVENT_UINH_BIT);
	if (flags & SBI_PMU_CFG_FLAG_SET_VSINH)
		bits |= BIT64(HPMEVENT_VSINH_BIT);
	if (flags & SBI_PMU_CFG_FLAG_SET_VUINH)
		bits |= BIT64(HPMEVENT_VUINH_BIT);
	return bits;
}

static void counter_start_one(struct pmu_hart *p, unsigned long idx)
{
	if (idx >= PMU_FW_FIRST) {
		p->fw_started |= (uint32_t)BIT(idx - PMU_FW_FIRST);
		return;
	}
	if (hart_has(HART_FEAT_SSCOFPMF) && idx >= 3) {
		/* Arm the overflow interrupt: OF clear. */
		hw_event_write((unsigned int)idx,
			       hw_event_read((unsigned int)idx) &
				       ~BIT64(HPMEVENT_OF_BIT));
	}
	csr_clear(CSR_MCOUNTINHIBIT, BIT(idx));
}

long pmu_counter_config(unsigned long base, unsigned long mask,
			unsigned long flags, unsigned long event_idx,
			uint64_t event_data, unsigned long *idx)
{
	struct pmu_hart *p = this_pmu();
	uint32_t event = (uint32_t)event_idx, candidates = 0;
	uint64_t select = 0;
	unsigned long c = ~UL(0);
	bool fw = EVENT_TYPE(event) == SBI_PMU_EVENT_TYPE_FW, is_fw = false;

	if (!counter_set_ok(base, mask) || event_idx > 0xfffff)
		return SBI_ERR_INVALID_PARAM;

	if (fw) {
		if (EVENT_CODE(event) >= SBI_PMU_FW_MAX)
			return SBI_ERR_NOT_SUPPORTED;
		candidates = ~U(0);
	} else {
		candidates = hw_event_lookup(event, event_data, &select) &
			     hw_counters;
		if (!candidates)
			return SBI_ERR_NOT_SUPPORTED;
	}

	if (flags & SBI_PMU_CFG_FLAG_SKIP_MATCH) {
		/*
		 * The caller names the counter: it must be one it configured.
		 */
		c = base + lowest_bit(mask);
		is_fw = c >= PMU_FW_FIRST;
		if (p->event[c] == EVENT_NONE || fw != is_fw)
			return SBI_ERR_INVALID_PARAM;
	} else {
		for (unsigned int i = 0; i < __RISCV_XLEN__ && c == ~UL(0);
		     i++) {
			unsigned long n = base + i;

			if (!(mask & BIT(i)) || p->event[n] != EVENT_NONE)
				continue;
			if (fw ? n >= PMU_FW_FIRST :
			    n < PMU_FW_FIRST && (candidates & BIT(n)))
				c = n;
		}
		if (c == ~UL(0))
			return SBI_ERR_NOT_SUPPORTED;
	}

	p->event[c] = event;
	if (fw) {
		if (flags & SBI_PMU_CFG_FLAG_CLEAR_VALUE)
			p->fw_value[c - PMU_FW_FIRST] = 0;
	} else {
		/* Reprogramming happens with the counter stopped. */
		csr_set(CSR_MCOUNTINHIBIT, BIT(c));
		if (hart_has(HART_FEAT_SSCOFPMF))
			select |= inhibit_bits(flags) | BIT64(HPMEVENT_OF_BIT);
		hw_event_write((unsigned int)c, select);
		if (flags & SBI_PMU_CFG_FLAG_CLEAR_VALUE)
			hw_counter_write((unsigned int)c, 0);
	}
	if (flags & SBI_PMU_CFG_FLAG_AUTO_START)
		counter_start_one(p, c);

	*idx = c;
	return SBI_SUCCESS;
}

long pmu_counter_start(unsigned long base, unsigned long mask,
		       unsigned long flags, uint64_t value)
{
	struct pmu_hart *p = this_pmu();
	const struct pmu_snapshot *snap = NULL;
	long rc = SBI_SUCCESS;

	if (!counter_set_ok(base, mask))
		return SBI_ERR_INVALID_PARAM;
	/* An initial value comes from the argument or from the snapshot. */
	if ((flags & SBI_PMU_START_FLAG_SET_INIT_VALUE) &&
	    (flags & SBI_PMU_START_FLAG_INIT_SNAPSHOT))
		return SBI_ERR_INVALID_PARAM;
	if (flags & SBI_PMU_START_FLAG_INIT_SNAPSHOT) {
		if (p->snapshot == SNAPSHOT_NONE)
			return SBI_ERR_NO_SHMEM;
		snap = smode_access_begin(p->snapshot, SNAPSHOT_SIZE);
	}

	for (unsigned int i = 0; i < __RISCV_XLEN__; i++) {
		unsigned long c = base + i;
		bool running = false;

		if (!(mask & BIT(i)))
			continue;
		if (p->event[c] == EVENT_NONE) {
			rc = SBI_ERR_INVALID_PARAM;
			continue;
		}
		running = c >= PMU_FW_FIRST ?
				  p->fw_started & BIT(c - PMU_FW_FIRST) :
				  hw_counter_running((unsigned int)c);
		if (running) {
			rc = SBI_ERR_ALREADY_STARTED;
			continue;
		}
		if (snap)
			value = snap->values[i];
		if (flags & (SBI_PMU_START_FLAG_SET_INIT_VALUE |
			     SBI_PMU_START_FLAG_INIT_SNAPSHOT)) {
			if (c >= PMU_FW_FIRST)
				p->fw_value[c - PMU_FW_FIRST] = value;
			else
				hw_counter_write((unsigned int)c, value);
		}
		counter_start_one(p, c);
	}
	if (snap)
		smode_access_end();
	return rc;
}

long pmu_counter_stop(unsigned long base, unsigned long mask,
		      unsigned long flags)
{
	struct pmu_hart *p = this_pmu();
	struct pmu_snapshot *snap = NULL;
	uint64_t overflown = 0;
	long rc = SBI_SUCCESS;

	if (!counter_set_ok(base, mask))
		return SBI_ERR_INVALID_PARAM;
	if (flags & SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT) {
		if (p->snapshot == SNAPSHOT_NONE)
			return SBI_ERR_NO_SHMEM;
		snap = smode_access_begin(p->snapshot, SNAPSHOT_SIZE);
	}

	for (unsigned int i = 0; i < __RISCV_XLEN__; i++) {
		unsigned long c = base + i;

		if (!(mask & BIT(i)))
			continue;
		if (c >= PMU_FW_FIRST) {
			if (!(p->fw_started & BIT(c - PMU_FW_FIRST)))
				rc = SBI_ERR_ALREADY_STOPPED;
			p->fw_started &= ~(uint32_t)BIT(c - PMU_FW_FIRST);
		} else {
			if (!hw_counter_running((unsigned int)c))
				rc = SBI_ERR_ALREADY_STOPPED;
			csr_set(CSR_MCOUNTINHIBIT, BIT(c));
		}
		if (snap && i < SNAPSHOT_VALUES) {
			/*
			 * The value, and with Sscofpmf whether it wrapped
			 * around.
			 */
			snap->values[i] =
				c >= PMU_FW_FIRST ?
					p->fw_value[c - PMU_FW_FIRST] :
					hw_counter_read((unsigned int)c);
			if (c >= 3 && c < PMU_FW_FIRST &&
			    hart_has(HART_FEAT_SSCOFPMF) &&
			    (hw_event_read((unsigned int)c) >>
			     HPMEVENT_OF_BIT) &
				    1)
				overflown |= BIT64(i);
		}
		if (flags & SBI_PMU_STOP_FLAG_RESET) {
			p->event[c] = EVENT_NONE;
			if (c < PMU_FW_FIRST) {
				hw_event_write((unsigned int)c, 0);
				/*
				 * Free again: cycle and instret run by default.
				 */
				if (c < 3)
					csr_clear(CSR_MCOUNTINHIBIT, BIT(c));
			}
		}
	}
	if (snap) {
		snap->overflow_bitmap = overflown;
		smode_access_end();
	}
	return rc;
}

bool pmu_hw_counter_get(unsigned int n, uint64_t *val)
{
	if (n >= PMU_HW_COUNTERS || !(hw_counters & BIT(n)))
		return false;
	*val = hw_counter_read(n);
	return true;
}

long pmu_snapshot_set_shmem(unsigned long lo, unsigned long hi,
			    unsigned long flags)
{
	struct pmu_hart *p = this_pmu();

	if (flags)
		return SBI_ERR_INVALID_PARAM;
	if (lo == ~UL(0) && hi == ~UL(0)) {
		p->snapshot = SNAPSHOT_NONE;
		return SBI_SUCCESS;
	}
	if (!IS_ALIGNED(lo, SNAPSHOT_SIZE))
		return SBI_ERR_INVALID_PARAM;
	if (hi || !smode_range_ok(lo, SNAPSHOT_SIZE))
		return SBI_ERR_INVALID_ADDRESS;
	p->snapshot = lo;
	return SBI_SUCCESS;
}

/*
 * Counter overflow as an SSE event rather than an interrupt S-mode can
 * mask: while the event is enabled the local counter overflow interrupt is
 * not delegated, and M-mode turns it into the event.
 */
bool pmu_sse_supported(void)
{
	return hart_has(HART_FEAT_SSCOFPMF);
}

void pmu_sse_enable(bool enable)
{
	struct pmu_hart *p = this_pmu();

	if (enable == p->sse_enabled)
		return;
	p->sse_enabled = enable;
	if (enable) {
		/*
		 * mie.LCOFIE is S-mode's sie bit while delegated: keep it for
		 * later.
		 */
		p->sse_saved_ie = csr_read(mie) & BIT(IRQ_PMU_OVF);
		csr_clear(mideleg, BIT(IRQ_PMU_OVF));
		csr_set(mie, BIT(IRQ_PMU_OVF));
	} else {
		csr_clear(mie, BIT(IRQ_PMU_OVF));
		csr_set(mideleg, BIT(IRQ_PMU_OVF));
		if (p->sse_saved_ie)
			csr_set(mie, BIT(IRQ_PMU_OVF));
	}
}

/* The overflow interrupt, in M-mode: quiet until the handler has completed. */
bool pmu_sse_overflow_irq(void)
{
	if (!this_pmu()->sse_enabled)
		return false;
	csr_clear(mie, BIT(IRQ_PMU_OVF));
	csr_clear(mip, BIT(IRQ_PMU_OVF));
	return true;
}

void pmu_sse_complete(void)
{
	if (this_pmu()->sse_enabled)
		csr_set(mie, BIT(IRQ_PMU_OVF));
}

long pmu_counter_fw_read(unsigned long idx, uint64_t *value)
{
	struct pmu_hart *p = this_pmu();

	if (idx < PMU_FW_FIRST || idx >= PMU_COUNTERS ||
	    p->event[idx] == EVENT_NONE)
		return SBI_ERR_INVALID_PARAM;
	*value = p->fw_value[idx - PMU_FW_FIRST];
	return SBI_SUCCESS;
}

/* Shared memory entry of event_get_info (SBI v3.0), little-endian. */
struct event_info {
	uint32_t event_idx;
	uint32_t output; /* bit 0: the event can be counted */
	uint64_t event_data;
};

long pmu_event_info(unsigned long addr, unsigned long count)
{
	struct event_info *info = NULL;
	uint64_t select = 0;

	if (!IS_ALIGNED(addr, sizeof(*info)))
		return SBI_ERR_INVALID_PARAM;
	if (count > ~UL(0) / sizeof(*info) ||
	    !smode_range_ok(addr, count * sizeof(*info)))
		return SBI_ERR_INVALID_ADDRESS;
	info = smode_access_begin(addr, count * sizeof(*info));

	for (unsigned long i = 0; i < count; i++) {
		uint32_t event = info[i].event_idx;
		bool ok = false;

		if (EVENT_TYPE(event) == SBI_PMU_EVENT_TYPE_FW)
			ok = EVENT_CODE(event) < SBI_PMU_FW_MAX;
		else
			ok = event <= 0xfffff &&
			     (hw_event_lookup(event, info[i].event_data,
					      &select) &
			      hw_counters);
		info[i].output = ok;
	}
	smode_access_end();
	return SBI_SUCCESS;
}
