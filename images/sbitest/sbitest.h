/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBITEST_H
#define SBITEST_H

#include <atomic.h>
#include <io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <util.h>

#include "sbicall.h"

/*
 * The entry points start.S and the monitor call, and what the tests reach
 * across files.
 */
void test_main(unsigned long hartid, unsigned long fdt);
void secondary_main(unsigned long hartid, unsigned long opaque);
void resume_main(unsigned long hartid, unsigned long opaque);
unsigned long strap_handler(unsigned long scause, unsigned long sepc,
			    unsigned long stval);
void _secondary_start(void);
void _resume_start(void);
struct sse_ctx;
void _sse_entry(void);
void sse_handler(struct sse_ctx *ctx, unsigned long hartid);
void dbtr_target(void);
long dbtr_icount_run(unsigned long ext, unsigned long fid);

/* QEMU's mtime ticks at 10 MHz; only the order of magnitude matters. */
#define TICKS_SHORT ULL(20000)
#define TICKS_TIMEOUT ULL(20000000)

/*
 * The payload keeps its harts by hart id, which the monitor does not bound:
 * ids below this get a stack and a mailbox, and that covers every QEMU virt
 * configuration, however few harts the monitor is built to manage.
 */
#define SBITEST_MAX_HARTS 16

/*
 * Where the monitor is: what it says in the device tree it hands us (the
 * "monitor@..." reservation), since it may not run where it was linked.
 */
extern unsigned long monitor_addr;

extern unsigned int checks, failures;

#define CHECK(cond, fmt, ...)                                                 \
	do {                                                                  \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                           \
			printf("  FAIL %s:%d: " fmt "\n", __func__, __LINE__, \
			       ##__VA_ARGS__);                                \
		}                                                             \
	} while (0)

#define CHECK_RET(ret, err) check_ret(__func__, __LINE__, (ret).error, err)
/* CHECK() of an SBI return's error, from where the macro is used. */
void check_ret(const char *func, int line, long error, long expected);

uint64_t now(void);
struct sbiret sbi_set_timer(uint64_t when);

/* Poll until cond or the timeout; evaluates to the final cond. */
#define WAIT_FOR(cond)                                  \
	({                                              \
		uint64_t __end = now() + TICKS_TIMEOUT; \
		bool __ok;                              \
							\
		do {                                    \
			__ok = (cond);                  \
		} while (!__ok && now() < __end);       \
		__ok;                                   \
	})

/*
 * puc.c: a model of an RPMI platform microcontroller, served by whichever
 * hart calls puc_poll(). puc_init() sets the shared memory queues up.
 */
#define PUC_IMPL_ID 0x7e57
#define PUC_IMPL_VERSION 0x00020003
#define PUC_NUM_CLOCKS 3
static inline uint64_t puc_clock_rate(uint32_t id)
{
	return ULL(1000000) * (id + 1) + SHIFT_U64(id, 32);
}

/* System MSIs of the model, and the one that prefers M-mode. */
#define PUC_NUM_SYSMSI 5
#define PUC_SYSMSI_MMODE 1
/*
 * Bit per system MSI the model was asked about: what the monitor let through.
 */
extern uint32_t puc_sysmsi_requests;

/* The model's own service group, for the sake of testing. */
#define PUC_GROUP_TEST CONFIG_QEMU_VIRT_RPMI_TEST_GROUP
#define PUC_TEST_POSTED 0xe0 /* posted: remember word 0 of the data */
#define PUC_TEST_SILENT 0xe1 /* never acknowledged */
#define PUC_TEST_STALE_ACK 0xe2 /* a foreign acknowledgment first */
#define PUC_TEST_NOTIFY 0xe3 /* word 0: number of events to send */
#define PUC_TEST_ECHO 0xe4 /* STATUS, then the request data */

#define PUC_EVENT_ID 0x01
#define PUC_EVENT_DATALEN 8 /* (sequence number, PUC_EVENT_MAGIC) */
#define PUC_EVENT_MAGIC 0xe7e27

/*
 * CPPC registers of the model: one read-only, one read-write, the rest absent.
 */
#define PUC_CPPC_REG_RO 0 /* HighestPerformance */
#define PUC_CPPC_REG_RW 5 /* DesiredPerformance */
#define PUC_CPPC_RO_VALUE 100
/* Fast channels: normal mode, with a doorbell that is a word of memory. */
#define PUC_CPPC_DB_VALUE 0x5a5a0001
extern uint32_t puc_cppc_doorbell, puc_cppc_writes;
/* The performance request fast channel of 'hart': (desired, reserved). */
uint32_t *puc_cppc_fastchan(unsigned long hart);

/* What the model saw of the M-mode consumers. */
extern uint32_t puc_hsm_starts, puc_hsm_stops, puc_hsm_last_hart;
extern uint32_t puc_hsm_suspends, puc_hsm_last_type;
/* The model's suspend types: platform specific ones, as the SBI sees them. */
#define PUC_SUSPEND_RET U(0x10000001)
#define PUC_SUSPEND_NON_RET U(0x90000001)
extern uint64_t puc_hsm_last_addr;
extern uint32_t puc_hsm_refuse; /* answer HSM_HART_START with DENIED */
extern uint32_t puc_reset_queries;

extern uint32_t puc_posted_value;
extern uint32_t puc_notifications_enabled;

void puc_init(void);
void puc_poll(void);

/*
 * Expected traps: while trap_expected is set the trap handler records an
 * exception and skips the instruction, which must be 4 bytes long.
 */
extern bool trap_expected;
extern unsigned long trap_cause, trap_tval, trap_count;

#define PROBE_INSN(insn, ...)                                             \
	({                                                                \
		__asm__ __volatile__(".option push\n.option norvc\n" insn \
				     "\n.option pop" __VA_ARGS__);        \
	})

/* domain.c, and what it needs of the secondary harts (main.c) */
void test_domains(unsigned long boot, unsigned long other);
void secondary_domain_enter(unsigned long hartid, unsigned long arg);
bool secondary_domain_returned(unsigned long hartid, long *error, long *value);
unsigned long secondary_ipis(unsigned long hartid);
void secondary_ipis_set(unsigned long hartid, unsigned long ipis);

/* dbtr.c */
void test_dbtr(void);

/* sse.c */
void test_sse(unsigned long self);
void test_sse_remote(unsigned long other);
void sse_secondary_setup(unsigned long hartid);

/* mpxy.c, rpmi.c: need another hart running puc_poll(). */
void test_mpxy(void);
void test_cppc(unsigned long self);

#endif
