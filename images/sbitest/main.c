// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI test payload. Runs in S-mode without translation, as the monitor's
 * next stage: the boot hart drives the tests and is the only one that
 * prints; the other harts are started through HSM and follow orders given
 * through a per-hart mailbox.
 */

#include <arch/csr.h>
#include <atomic.h>
#include <io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <util.h>

#ifdef CONFIG_MONITOR_FDT_FIXUP
#include <libfdt.h>
#endif

#include "sbicall.h"
#include "sbitest.h"

#define SIP_SSIP BIT(IRQ_S_SOFT)
#define SIP_STIP BIT(IRQ_S_TIMER)
#define SSTATUS_SIE BIT(1)

#define MAX_HARTS CONFIG_PLATFORM_HART_COUNT
#define BOGUS_EID UL(0x0badc0de)
#define MAGIC UL(0x5b17e57)

/* ---- bookkeeping ------------------------------------------------------ */

unsigned int checks, failures;

void check_ret(const char *func, int line, long error, long expected)
{
	checks++;
	if (error != expected) {
		failures++;
		printf("  FAIL %s:%d: error %ld, expected %ld\n", func, line,
		       error, expected);
	}
}

uint64_t now(void)
{
#if __RISCV_XLEN__ == 32
	uint32_t hi = 0, lo = 0;

	do {
		hi = csr_read(timeh);
		lo = csr_read(time);
	} while (hi != csr_read(timeh));
	return reg_pair_to_64(hi, lo);
#else
	return csr_read(time);
#endif
}

static struct sbiret sbi_set_timer(uint64_t when)
{
#if __RISCV_XLEN__ == 32
	return sbi_call2(SBI_EXT_TIME, SBI_TIME_SET_TIMER, (unsigned long)when,
			 (unsigned long)(when >> 32));
#else
	return sbi_call1(SBI_EXT_TIME, SBI_TIME_SET_TIMER, when);
#endif
}

static long hart_status(unsigned long hartid)
{
	struct sbiret ret =
		sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, hartid);

	return ret.error ? ret.error : ret.value;
}

/* ---- trap handling ---------------------------------------------------- */

static unsigned long boot_hartid;
static bool trap_expected;
static unsigned long trap_cause, trap_tval, trap_count;
static unsigned long timer_irqs, soft_irqs;

unsigned long strap_handler(unsigned long scause, unsigned long sepc,
			    unsigned long stval)
{
	if (scause & CAUSE_IRQ_FLAG) {
		switch (scause & ~CAUSE_IRQ_FLAG) {
		case IRQ_S_TIMER:
			timer_irqs++;
			sbi_set_timer(~ULL(0));
			return sepc;
		case IRQ_S_SOFT:
			soft_irqs++;
			csr_clear(sip, SIP_SSIP);
			return sepc;
		default:
			break;
		}
	} else if (trap_expected) {
		/* The probes below only fault on 4-byte instructions. */
		WRITE_ONCE(trap_cause, scause);
		WRITE_ONCE(trap_tval, stval);
		trap_count++;
		return sepc + 4;
	}

	printf("sbitest: unexpected trap scause=%lx sepc=%lx stval=%lx\n",
	       scause, sepc, stval);
	printf("sbitest: FAIL\n");
	sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET, SBI_SRST_TYPE_SHUTDOWN,
		  SBI_SRST_REASON_SYSTEM_FAILURE);
	for (;;)
		;
}

#define PROBE_INSN(insn, ...)                                             \
	({                                                                \
		__asm__ __volatile__(".option push\n.option norvc\n" insn \
				     "\n.option pop" __VA_ARGS__);        \
	})

/* ---- mailbox ---------------------------------------------------------- */

enum cmd {
	CMD_NONE,
	CMD_STOP,
	CMD_SUSPEND_RETENTIVE,
	CMD_SUSPEND_NON_RETENTIVE,
	CMD_PUC, /* serve the RPMI PuC model from now on */
};

struct mailbox {
	unsigned long cmd;
	unsigned long started; /* opaque seen by _secondary_start */
	unsigned long resumed; /* opaque seen by _resume_start */
	unsigned long ipis;
	unsigned long suspend_ret; /* retentive suspend returned */
	long suspend_error;
	unsigned long puc; /* serving the PuC model */
};

static struct mailbox mbox[MAX_HARTS];

static void __noreturn secondary_loop(unsigned long hartid)
{
	struct mailbox *m = &mbox[hartid];
	struct sbiret ret = {};

	for (;;) {
		if (csr_read(sip) & SIP_SSIP) {
			csr_clear(sip, SIP_SSIP);
			atomic_add_ulong(&m->ipis, 1);
		}

		if (READ_ONCE(m->puc))
			puc_poll();

		switch (READ_ONCE(m->cmd)) {
		case CMD_PUC:
			WRITE_ONCE(m->cmd, CMD_NONE);
			puc_init();
			WRITE_ONCE(m->puc, 1);
			break;
		case CMD_STOP:
			WRITE_ONCE(m->cmd, CMD_NONE);
			WRITE_ONCE(m->puc, 0);
			sbi_call0(SBI_EXT_HSM, SBI_HSM_HART_STOP);
			break;
		case CMD_SUSPEND_RETENTIVE:
			WRITE_ONCE(m->cmd, CMD_NONE);
			csr_write(sie, SIP_SSIP);
			ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND,
					SBI_HSM_SUSPEND_RET_DEFAULT, 0, 0);
			csr_write(sie, 0);
			WRITE_ONCE(m->suspend_error, ret.error);
			atomic_add_ulong(&m->suspend_ret, 1);
			break;
		case CMD_SUSPEND_NON_RETENTIVE:
			WRITE_ONCE(m->cmd, CMD_NONE);
			csr_write(sie, SIP_SSIP);
			ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND,
					SBI_HSM_SUSPEND_NON_RET_DEFAULT,
					(unsigned long)_resume_start,
					MAGIC ^ hartid);
			/* Only reached when the call failed. */
			WRITE_ONCE(m->suspend_error, ret.error);
			atomic_add_ulong(&m->suspend_ret, 1);
			break;
		default:
			break;
		}
	}
}

void secondary_main(unsigned long hartid, unsigned long opaque)
{
	WRITE_ONCE(mbox[hartid].started, opaque);
	secondary_loop(hartid);
}

static void system_resumed(unsigned long opaque);

void resume_main(unsigned long hartid, unsigned long opaque)
{
	if (hartid == boot_hartid)
		system_resumed(opaque);
	WRITE_ONCE(mbox[hartid].resumed, opaque);
	secondary_loop(hartid);
}

/* ---- tests ------------------------------------------------------------ */

static void test_base(void)
{
	static const unsigned long exts[] = {
		SBI_EXT_BASE,
		SBI_EXT_TIME,
		SBI_EXT_IPI,
		SBI_EXT_RFENCE,
		SBI_EXT_HSM,
		SBI_EXT_SRST,
		SBI_EXT_DBCN,
#ifdef CONFIG_SBI_LEGACY
		SBI_EXT_LEGACY_SET_TIMER,
		SBI_EXT_LEGACY_SHUTDOWN,
#endif
	};
	struct sbiret ret = {};

	printf("base\n");
	ret = sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_SPEC_VERSION);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value ==
	      ((SBI_SPEC_VERSION_MAJOR << 24) | SBI_SPEC_VERSION_MINOR),
	      "spec version %lx", ret.value);
	CHECK(!((unsigned long)ret.value & BIT(31)),
	      "spec version bit 31 must be 0");

	ret = sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_IMPL_ID);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == CONFIG_SBI_IMPL_ID, "impl id %lx", ret.value);
	ret = sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_IMPL_VERSION);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == CONFIG_SBI_IMPL_VERSION, "impl version %lx",
	      ret.value);

	for (unsigned int i = 0; i < ARRAY_SIZE(exts); i++) {
		ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION,
				exts[i]);
		CHECK_RET(ret, SBI_SUCCESS);
		CHECK(ret.value != 0, "extension %lx not probed", exts[i]);
	}
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, BOGUS_EID);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 0, "bogus extension probed");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_SSE);
	CHECK(ret.value == 0, "SSE probed but not implemented");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_FWFT);
	CHECK(ret.value != 0, "FWFT not probed");

	CHECK_RET(sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_MVENDORID), SBI_SUCCESS);
	CHECK_RET(sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_MARCHID), SBI_SUCCESS);
	CHECK_RET(sbi_call0(SBI_EXT_BASE, SBI_BASE_GET_MIMPID), SBI_SUCCESS);

	CHECK_RET(sbi_call0(SBI_EXT_BASE, 100), SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call0(BOGUS_EID, 0), SBI_ERR_NOT_SUPPORTED);
}

static void test_dbcn(void)
{
	static const char msg[] = "  (written with console_write)\r\n";
	char buf[8] = {};
	struct sbiret ret = {};

	printf("dbcn\n");
	ret = sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, sizeof(msg) - 1,
			(unsigned long)msg, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == sizeof(msg) - 1, "wrote %ld bytes", ret.value);

	ret = sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 0,
			(unsigned long)msg, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 0, "empty write wrote %ld bytes", ret.value);

	/* The monitor must not print (or fill) its own memory for us. */
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 16,
			    CONFIG_MONITOR_LOAD_ADDR, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_READ, 16,
			    CONFIG_MONITOR_LOAD_ADDR + 0x1000, 0),
		  SBI_ERR_INVALID_PARAM);
	/* A buffer that starts below the monitor and runs into it. */
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 32,
			    CONFIG_MONITOR_LOAD_ADDR - 16, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 16,
			    (unsigned long)msg, 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, ~UL(0),
			    (unsigned long)msg, 0),
		  SBI_ERR_INVALID_PARAM);

	ret = sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_READ, sizeof(buf),
			(unsigned long)buf, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value >= 0 && ret.value <= (long)sizeof(buf),
	      "read %ld bytes", ret.value);

	CHECK_RET(sbi_call1(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE, ' '),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call0(SBI_EXT_DBCN, 3), SBI_ERR_NOT_SUPPORTED);
}

static void test_time(void)
{
	uint64_t t0 = now(), t1 = 0;

	printf("time\n");
	t1 = now();
	CHECK(t1 >= t0, "time went backwards");

	/* Polled: the interrupt becomes pending at the deadline, not before. */
	CHECK_RET(sbi_set_timer(~ULL(0)), SBI_SUCCESS);
	CHECK(!(csr_read(sip) & SIP_STIP), "STIP pending with no deadline");
	t0 = now();
	CHECK_RET(sbi_set_timer(t0 + TICKS_SHORT), SBI_SUCCESS);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_STIP), "STIP never became pending");
	CHECK(now() >= t0 + TICKS_SHORT, "STIP pending before the deadline");
	CHECK_RET(sbi_set_timer(~ULL(0)), SBI_SUCCESS);
	CHECK(!(csr_read(sip) & SIP_STIP),
	      "STIP not cleared by a new deadline");

	/* A deadline in the past fires at once. */
	CHECK_RET(sbi_set_timer(0), SBI_SUCCESS);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_STIP), "past deadline did not fire");
	CHECK_RET(sbi_set_timer(~ULL(0)), SBI_SUCCESS);

	/* Delivered: the handler counts it and pushes the deadline away. */
	WRITE_ONCE(timer_irqs, 0);
	csr_write(sie, SIP_STIP);
	csr_set(sstatus, SSTATUS_SIE);
	CHECK_RET(sbi_set_timer(now() + TICKS_SHORT), SBI_SUCCESS);
	CHECK(WAIT_FOR(READ_ONCE(timer_irqs)), "timer interrupt not delivered");
	csr_clear(sstatus, SSTATUS_SIE);
	csr_write(sie, 0);
	CHECK(READ_ONCE(timer_irqs) == 1, "%lu timer interrupts",
	      READ_ONCE(timer_irqs));

	CHECK_RET(sbi_call0(SBI_EXT_TIME, 1), SBI_ERR_NOT_SUPPORTED);
}

static void test_ipi_self(void)
{
	unsigned long self = boot_hartid;

	printf("ipi (self)\n");
	csr_clear(sip, SIP_SSIP);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 1, self),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_SSIP), "SSIP not pending");
	csr_clear(sip, SIP_SSIP);
	CHECK(!(csr_read(sip) & SIP_SSIP), "SSIP cannot be cleared");

	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, BIT(self), 0),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_SSIP), "SSIP not pending (base 0)");
	csr_clear(sip, SIP_SSIP);

	WRITE_ONCE(soft_irqs, 0);
	csr_write(sie, SIP_SSIP);
	csr_set(sstatus, SSTATUS_SIE);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 0, ~UL(0)),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(READ_ONCE(soft_irqs)),
	      "software interrupt not delivered");
	csr_clear(sstatus, SSTATUS_SIE);
	csr_write(sie, 0);

	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 1, MAX_HARTS),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 1, ~UL(0) - 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call0(SBI_EXT_IPI, 1), SBI_ERR_NOT_SUPPORTED);
}

#ifdef CONFIG_SBI_LEGACY
static void test_legacy(void)
{
	unsigned long mask = BIT(boot_hartid);
	struct sbiret ret = {};

	printf("legacy\n");
	/* v0.1 calls return in a0 only: a1 comes back as it went in. */
	ret = sbi_call2(SBI_EXT_LEGACY_PUTCHAR, 0, ' ', MAGIC);
	CHECK(ret.error == 0 && ret.value == (long)MAGIC,
	      "putchar: a0=%ld a1=%lx", ret.error, ret.value);
	ret = sbi_call2(SBI_EXT_LEGACY_GETCHAR, 0, 0, MAGIC);
	CHECK(ret.error >= -1 && ret.error <= 255 && ret.value == (long)MAGIC,
	      "getchar: a0=%ld a1=%lx", ret.error, ret.value);

	csr_clear(sip, SIP_SSIP);
	ret = sbi_call1(SBI_EXT_LEGACY_SEND_IPI, 0, (unsigned long)&mask);
	CHECK(ret.error == 0, "send_ipi: %ld", ret.error);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_SSIP), "legacy IPI not pending");
	ret = sbi_call0(SBI_EXT_LEGACY_CLEAR_IPI, 0);
	CHECK(ret.error == 0, "clear_ipi: %ld", ret.error);
	CHECK(!(csr_read(sip) & SIP_SSIP),
	      "legacy clear_ipi left SSIP pending");

	/*
	 * The mask is read the way we would read it: a bad pointer faults here.
	 */
	WRITE_ONCE(trap_expected, true);
	WRITE_ONCE(trap_count, 0);
	sbi_call1(SBI_EXT_LEGACY_SEND_IPI, 0, CONFIG_MONITOR_LOAD_ADDR);
	WRITE_ONCE(trap_expected, false);
	CHECK(trap_count == 1 && trap_cause == CAUSE_LOAD_ACCESS,
	      "bad hart mask pointer: %lu traps, cause %lu", trap_count,
	      trap_cause);

#if __RISCV_XLEN__ == 32
	ret = sbi_call2(SBI_EXT_LEGACY_SET_TIMER, 0, 0, 0);
#else
	ret = sbi_call1(SBI_EXT_LEGACY_SET_TIMER, 0, 0);
#endif
	CHECK(ret.error == 0, "set_timer: %ld", ret.error);
	CHECK(WAIT_FOR(csr_read(sip) & SIP_STIP), "legacy timer did not fire");
	sbi_set_timer(~ULL(0));

	ret = sbi_call3(SBI_EXT_LEGACY_RFENCE_I, 0, (unsigned long)&mask, 0, 0);
	CHECK(ret.error == 0, "remote_fence_i: %ld", ret.error);
	ret = sbi_call3(SBI_EXT_LEGACY_SFENCE_VMA, 0, 0, 0, 0x1000);
	CHECK(ret.error == 0, "remote_sfence_vma: %ld", ret.error);
}

#else
static void test_legacy(void)
{
	CHECK_RET(sbi_call1(SBI_EXT_LEGACY_PUTCHAR, 0, ' '),
		  SBI_ERR_NOT_SUPPORTED);
}
#endif

static void test_traps(void)
{
	unsigned long val = 0;

	printf("traps\n");
	WRITE_ONCE(trap_expected, true);

	/* An M-mode CSR: illegal instruction, handed back by the monitor. */
	WRITE_ONCE(trap_count, 0);
	PROBE_INSN("csrr %0, mstatus", : "=r"(val) : : "memory");
	CHECK(trap_count == 1 && trap_cause == CAUSE_ILLEGAL_INSN,
	      "M-mode CSR read: %lu traps, cause %lu", trap_count, trap_cause);

	/*
	 * Monitor memory is fenced off by PMP, for reads, writes and fetches.
	 */
	WRITE_ONCE(trap_count, 0);
	PROBE_INSN("lbu %0, 0(%1)",
		   : "=r"(val)
		   : "r"((unsigned long)CONFIG_MONITOR_LOAD_ADDR)
		   : "memory");
	CHECK(trap_count == 1 && trap_cause == CAUSE_LOAD_ACCESS,
	      "monitor read: %lu traps, cause %lu", trap_count, trap_cause);
	CHECK(trap_tval == CONFIG_MONITOR_LOAD_ADDR, "stval %lx", trap_tval);

	WRITE_ONCE(trap_count, 0);
	PROBE_INSN("sb zero, 0(%0)",
		   :
		   : "r"((unsigned long)(CONFIG_MONITOR_LOAD_ADDR +
					 CONFIG_MONITOR_SIZE - 1))
		   : "memory");
	CHECK(trap_count == 1 && trap_cause == CAUSE_STORE_ACCESS,
	      "monitor write: %lu traps, cause %lu", trap_count, trap_cause);

	/* ... and the memory right after it is ours. */
	WRITE_ONCE(trap_count, 0);
	PROBE_INSN("lbu %0, 0(%1)",
		   : "=r"(val)
		   : "r"((unsigned long)(CONFIG_MONITOR_LOAD_ADDR +
					 CONFIG_MONITOR_SIZE))
		   : "memory");
	CHECK(trap_count == 0, "read past the monitor trapped, cause %lu",
	      trap_cause);

	WRITE_ONCE(trap_expected, false);
}

static unsigned int start_secondaries(void)
{
	unsigned int started = 0;
	struct sbiret ret = {};

	for (unsigned long h = 0; h < MAX_HARTS; h++) {
		long status = hart_status(h);

		if (h == boot_hartid) {
			CHECK(status == SBI_HSM_STATE_STARTED,
			      "boot hart status %ld", status);
			continue;
		}
		if (status == SBI_ERR_INVALID_PARAM)
			continue;
		CHECK(status == SBI_HSM_STATE_STOPPED, "hart %lu status %ld", h,
		      status);

		WRITE_ONCE(mbox[h].started, 0);
		ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, h,
				(unsigned long)_secondary_start, MAGIC + h);
		CHECK_RET(ret, SBI_SUCCESS);
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].started)),
		      "hart %lu did not start", h);
		CHECK(READ_ONCE(mbox[h].started) == MAGIC + h,
		      "hart %lu opaque %lx", h, READ_ONCE(mbox[h].started));
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_STARTED),
		      "hart %lu status %ld after start", h, hart_status(h));
		started++;
	}
	return started;
}

static void secondaries_cmd(enum cmd cmd)
{
	for (unsigned long h = 0; h < MAX_HARTS; h++)
		if (h != boot_hartid && READ_ONCE(mbox[h].started))
			WRITE_ONCE(mbox[h].cmd, cmd);
}

/* The started secondary harts, in order: the first one from 'from' on. */
static unsigned long next_secondary(unsigned long from)
{
	for (; from < MAX_HARTS; from++)
		if (from != boot_hartid && READ_ONCE(mbox[from].started))
			break;
	return from;
}

static void test_hsm_errors(unsigned long other)
{
	struct sbiret ret = {};

	CHECK_RET(sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, MAX_HARTS),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, ~UL(0)),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, MAX_HARTS,
			    (unsigned long)_secondary_start, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, boot_hartid,
			    (unsigned long)_secondary_start, 0),
		  SBI_ERR_ALREADY_AVAILABLE);
	if (other != ~UL(0)) {
		CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, other,
				    (unsigned long)_secondary_start, 0),
			  SBI_ERR_ALREADY_AVAILABLE);
	}

	/*
	 * Reserved and unimplemented suspend types; a resume address we do not
	 * own.
	 */
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND, UL(0x0fffffff), 0,
			0);
	CHECK_RET(ret, SBI_ERR_INVALID_PARAM);
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND, UL(0x80000001), 0,
			0);
	CHECK_RET(ret, SBI_ERR_INVALID_PARAM);
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND, UL(0x7fffffff), 0,
			0);
	CHECK_RET(ret, SBI_ERR_NOT_SUPPORTED);
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND,
			SBI_HSM_SUSPEND_RET_PLATFORM, 0, 0);
	CHECK_RET(ret, SBI_ERR_NOT_SUPPORTED);
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND,
			SBI_HSM_SUSPEND_NON_RET_DEFAULT,
			CONFIG_MONITOR_LOAD_ADDR, 0);
	CHECK_RET(ret, SBI_ERR_INVALID_ADDRESS);
	CHECK(hart_status(boot_hartid) == SBI_HSM_STATE_STARTED,
	      "failed suspend changed the hart state");
	CHECK_RET(sbi_call0(SBI_EXT_HSM, 4), SBI_ERR_NOT_SUPPORTED);
}

static void test_suspend_self(void)
{
	uint64_t t0 = now();
	struct sbiret ret = {};

	/* Retentive suspend of the calling hart, woken by its timer. */
	csr_write(sie, SIP_STIP);
	CHECK_RET(sbi_set_timer(t0 + TICKS_SHORT), SBI_SUCCESS);
	ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_SUSPEND,
			SBI_HSM_SUSPEND_RET_DEFAULT, 0, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(now() >= t0 + TICKS_SHORT, "suspend returned before the timer");
	CHECK(csr_read(sip) & SIP_STIP, "woken up without a pending timer");
	sbi_set_timer(~ULL(0));
	csr_write(sie, 0);
	CHECK(hart_status(boot_hartid) == SBI_HSM_STATE_STARTED,
	      "status %ld after resume", hart_status(boot_hartid));
}

static void test_smp(void)
{
	unsigned int secondaries = 0;
	unsigned long h = 0, first = ~UL(0), all = 0;
	struct sbiret ret = {};

	printf("hsm start\n");
	secondaries = start_secondaries();
	printf("  %u secondary hart(s)\n", secondaries);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1)) {
		if (first == ~UL(0))
			first = h;
		all |= BIT(h);
	}
	test_hsm_errors(first);
	if (first != ~UL(0))
		CHECK_RET(sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, first,
				    CONFIG_MONITOR_LOAD_ADDR, 0),
			  SBI_ERR_INVALID_ADDRESS);

	printf("hsm suspend (self)\n");
	test_suspend_self();

	printf("ipi (remote)\n");
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		WRITE_ONCE(mbox[h].ipis, 0);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, all, 0),
		  SBI_SUCCESS);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].ipis) == 1),
		      "hart %lu: %lu IPIs", h, READ_ONCE(mbox[h].ipis));
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, 0, ~UL(0)),
		  SBI_SUCCESS);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].ipis) == 2),
		      "hart %lu: %lu IPIs (all)", h, READ_ONCE(mbox[h].ipis));
	csr_clear(sip, SIP_SSIP);

	/* One of the secondaries plays the platform microcontroller. */
	if (first != ~UL(0)) {
		WRITE_ONCE(mbox[first].cmd, CMD_PUC);
		CHECK(WAIT_FOR(READ_ONCE(mbox[first].puc)),
		      "hart %lu: no PuC model", first);
		test_mpxy();
	}

	printf("rfence\n");
	for (unsigned long fid = SBI_RFENCE_FENCE_I;
	     fid <= SBI_RFENCE_HFENCE_VVMA; fid++) {
		/* A short range, a long one, and everything. */
		static const unsigned long sizes[] = { 0x3000, 0x4000000,
						       ~UL(0) };

		for (unsigned int i = 0; i < ARRAY_SIZE(sizes); i++) {
			ret = sbi_call(SBI_EXT_RFENCE, fid, 0, ~UL(0),
				       UL(0x80000000), sizes[i], 1);
			/* The hfence calls need the H extension. */
			CHECK(ret.error == SBI_SUCCESS ||
			      (fid >= SBI_RFENCE_HFENCE_GVMA_VMID &&
			       ret.error == SBI_ERR_NOT_SUPPORTED),
			      "fid %lu size %lx: error %ld", fid, sizes[i],
			      ret.error);
		}
	}
	ret = sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_SFENCE_VMA, all, 0, 0, 0x1000,
		       0);
	CHECK_RET(ret, SBI_SUCCESS);
	ret = sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_SFENCE_VMA, 1, MAX_HARTS, 0,
		       0x1000, 0);
	CHECK_RET(ret, SBI_ERR_INVALID_PARAM);
	ret = sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_SFENCE_VMA, 0, ~UL(0),
		       ~UL(0) - 0x1000, 0x10000, 0);
	CHECK_RET(ret, SBI_ERR_INVALID_ADDRESS);
	CHECK_RET(sbi_call0(SBI_EXT_RFENCE, 7), SBI_ERR_NOT_SUPPORTED);
	/* The fences went through M-mode IPIs: none may leak into S-mode. */
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(mbox[h].ipis == 2, "hart %lu: fence seen as IPI", h);

	printf("hsm suspend (retentive)\n");
	secondaries_cmd(CMD_SUSPEND_RETENTIVE);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_SUSPENDED),
		      "hart %lu status %ld", h, hart_status(h));
	/* A fence must not wake a suspended hart up, an IPI must. */
	ret = sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_FENCE_I, 0, ~UL(0), 0, 0, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(hart_status(h) == SBI_HSM_STATE_SUSPENDED &&
		      !READ_ONCE(mbox[h].suspend_ret),
		      "hart %lu woken by a fence", h);
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, all, 0),
		  SBI_SUCCESS);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1)) {
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].suspend_ret) == 1),
		      "hart %lu still suspended", h);
		CHECK(READ_ONCE(mbox[h].suspend_error) == SBI_SUCCESS,
		      "hart %lu suspend error %ld", h,
		      READ_ONCE(mbox[h].suspend_error));
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_STARTED),
		      "hart %lu status %ld", h, hart_status(h));
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].ipis) == 3),
		      "hart %lu lost its wake-up IPI", h);
	}

	printf("hsm suspend (non-retentive)\n");
	secondaries_cmd(CMD_SUSPEND_NON_RETENTIVE);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_SUSPENDED),
		      "hart %lu status %ld", h, hart_status(h));
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, all, 0),
		  SBI_SUCCESS);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1)) {
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].resumed)),
		      "hart %lu did not resume", h);
		CHECK(READ_ONCE(mbox[h].resumed) == (MAGIC ^ h),
		      "hart %lu opaque %lx", h, READ_ONCE(mbox[h].resumed));
		CHECK(READ_ONCE(mbox[h].suspend_ret) == 1,
		      "hart %lu: suspend returned", h);
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_STARTED),
		      "hart %lu status %ld", h, hart_status(h));
		CHECK(WAIT_FOR(READ_ONCE(mbox[h].ipis) == 4),
		      "hart %lu lost its wake-up IPI", h);
	}

	printf("hsm stop\n");
	secondaries_cmd(CMD_STOP);
	for (h = next_secondary(0); h < MAX_HARTS; h = next_secondary(h + 1))
		CHECK(WAIT_FOR(hart_status(h) == SBI_HSM_STATE_STOPPED),
		      "hart %lu status %ld", h, hart_status(h));
	/* Stopped harts are skipped, not an error. */
	CHECK_RET(sbi_call2(SBI_EXT_IPI, SBI_IPI_SEND_IPI, all, 0),
		  SBI_SUCCESS);
	ret = sbi_call(SBI_EXT_RFENCE, SBI_RFENCE_SFENCE_VMA, all, 0, 0, 0, 0);
	CHECK_RET(ret, SBI_SUCCESS);

	if (first != ~UL(0)) {
		printf("hsm restart\n");
		WRITE_ONCE(mbox[first].started, 0);
		WRITE_ONCE(mbox[first].ipis, 0);
		ret = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START, first,
				(unsigned long)_secondary_start, MAGIC);
		CHECK_RET(ret, SBI_SUCCESS);
		CHECK(WAIT_FOR(READ_ONCE(mbox[first].started) == MAGIC),
		      "hart %lu did not restart", first);
		/* The IPI sent while it was stopped must not show up now. */
		CHECK(READ_ONCE(mbox[first].ipis) == 0,
		      "stale IPI after restart");
		WRITE_ONCE(mbox[first].cmd, CMD_STOP);
		CHECK(WAIT_FOR(hart_status(first) == SBI_HSM_STATE_STOPPED),
		      "hart %lu status %ld", first, hart_status(first));
	}
}

#ifdef CONFIG_MONITOR_FDT_FIXUP
/* The device tree we were given must keep us away from the monitor. */
static void test_fdt(unsigned long addr)
{
	const void *fdt = (const void *)addr;
	int parent = 0, node = 0, found = 0;

	printf("fdt\n");
	CHECK(addr && fdt_check_header(fdt) == 0, "no valid device tree at %lx",
	      addr);
	if (!addr || fdt_check_header(fdt))
		return;
	CHECK(fdt_check_full(fdt, fdt_totalsize(fdt)) == 0,
	      "fdt_check_full failed");

	parent = fdt_path_offset(fdt, "/reserved-memory");
	CHECK(parent >= 0, "no /reserved-memory node");
	if (parent < 0)
		return;
	CHECK(fdt_getprop(fdt, parent, "ranges", NULL),
	      "/reserved-memory without ranges");

	fdt_for_each_subnode(node, fdt, parent) {
		int ac = fdt_address_cells(fdt, parent);
		int sc = fdt_size_cells(fdt, parent);
		uint64_t base = 0, size = 0;
		const fdt32_t *reg = NULL;
		int len = 0;

		reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg || len != (ac + sc) * (int)sizeof(*reg))
			continue;
		for (int i = 0; i < ac; i++)
			base = (base << 32) | fdt32_to_cpu(reg[i]);
		for (int i = 0; i < sc; i++)
			size = (size << 32) | fdt32_to_cpu(reg[ac + i]);
		if (base != CONFIG_MONITOR_LOAD_ADDR ||
		    size != CONFIG_MONITOR_SIZE)
			continue;
		found++;
		CHECK(fdt_getprop(fdt, node, "no-map", NULL),
		      "monitor memory is not no-map");
	}
	CHECK(found == 1, "%d reserved-memory entries for the monitor", found);
}
#else
static void test_fdt(unsigned long addr)
{
}
#endif

#ifdef CONFIG_SBI_PMU
#define FW_EVENT(code) ((SBI_PMU_EVENT_TYPE_FW << 16) | (code))
#define PMU_CFG_START \
	(SBI_PMU_CFG_FLAG_CLEAR_VALUE | SBI_PMU_CFG_FLAG_AUTO_START)

static struct sbiret pmu_config(unsigned long base, unsigned long mask,
				unsigned long flags, unsigned long event)
{
	/* event_data is 64 bits wide: one register on RV64, two on RV32. */
	return sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_CONFIG_MATCHING, base,
			mask, flags, event, 0);
}

static uint64_t pmu_fw_read(unsigned long idx)
{
	struct sbiret lo = sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ, idx);
	struct sbiret hi =
		sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ_HI, idx);

	CHECK_RET(lo, SBI_SUCCESS);
	CHECK_RET(hi, SBI_SUCCESS);
#if __RISCV_XLEN__ == 32
	return ((uint64_t)(unsigned long)hi.value << 32) |
	       (unsigned long)lo.value;
#else
	CHECK(hi.value == 0, "fw_read_hi %lx on RV64", hi.value);
	return (uint64_t)lo.value;
#endif
}

static void pmu_stop_reset(unsigned long idx)
{
	sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, idx, 1,
		  SBI_PMU_STOP_FLAG_RESET);
}

static void test_pmu(void)
{
	unsigned long total = 0, fw_first = ~UL(0), fw_mask = 0, hw = 0, fw = 0,
		      idx = 0, val = 0;
	struct sbiret ret = {};
	uint64_t c0 = 0, c1 = 0;

	printf("pmu\n");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_PMU);
	CHECK(ret.value != 0, "PMU not probed");

	ret = sbi_call0(SBI_EXT_PMU, SBI_PMU_NUM_COUNTERS);
	CHECK_RET(ret, SBI_SUCCESS);
	total = (unsigned long)ret.value;
	for (unsigned long i = 0; i < total; i++) {
		ret = sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_GET_INFO, i);
		if (ret.error)
			continue;
		if ((unsigned long)ret.value >> (__RISCV_XLEN__ - 1)) {
			fw++;
			if (fw_first == ~UL(0))
				fw_first = i;
		} else {
			hw++;
			CHECK(((unsigned long)ret.value & 0xfff) == 0xc00 + i,
			      "counter %lu: info %lx", i, ret.value);
		}
	}
	printf("  %lu hardware, %lu firmware counters\n", hw, fw);
	CHECK(fw > 0 && fw < __RISCV_XLEN__, "%lu firmware counters", fw);
	fw_mask = BIT(fw) - 1;
	/* A counter set that reaches past the last counter is invalid. */
	CHECK_RET(pmu_config(fw_first, ~UL(0), 0,
			     FW_EVENT(SBI_PMU_FW_SET_TIMER)),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_GET_INFO, total),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_GET_INFO, 1),
		  SBI_ERR_INVALID_PARAM);

	/* A firmware counter on set_timer calls. */
	ret = pmu_config(fw_first, fw_mask, PMU_CFG_START,
			 FW_EVENT(SBI_PMU_FW_SET_TIMER));
	CHECK_RET(ret, SBI_SUCCESS);
	idx = (unsigned long)ret.value;
	CHECK(idx >= fw_first && idx < total, "firmware counter index %lu",
	      idx);
	for (int i = 0; i < 3; i++)
		sbi_set_timer(~ULL(0));
	CHECK(pmu_fw_read(idx) == 3, "counted %lu set_timer calls",
	      (unsigned long)pmu_fw_read(idx));
	CHECK_RET(sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_START, idx, 1, 0, 0, 0),
		  SBI_ERR_ALREADY_STARTED);
	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, idx, 1, 0),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, idx, 1, 0),
		  SBI_ERR_ALREADY_STOPPED);
	sbi_set_timer(~ULL(0));
	CHECK(pmu_fw_read(idx) == 3, "stopped counter moved");
	/* Restart from an initial value (64 bits: a3, plus a4 on RV32). */
	ret = sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_START, idx, 1,
		       SBI_PMU_START_FLAG_SET_INIT_VALUE, UL(0xffffffff), 0);
	CHECK_RET(ret, SBI_SUCCESS);
	sbi_set_timer(~ULL(0));
	CHECK(pmu_fw_read(idx) == ULL(0x100000000),
	      "no carry into the upper half");

	/* A second one, on the traps the monitor hands back to us. */
	ret = pmu_config(fw_first, fw_mask, PMU_CFG_START,
			 FW_EVENT(SBI_PMU_FW_ILLEGAL_INSN));
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK((unsigned long)ret.value != idx, "counter %lu handed out twice",
	      idx);
	WRITE_ONCE(trap_expected, true);
	PROBE_INSN("csrr %0, mstatus", : "=r"(val) : : "memory");
	PROBE_INSN("csrr %0, mstatus", : "=r"(val) : : "memory");
	WRITE_ONCE(trap_expected, false);
	CHECK(pmu_fw_read((unsigned long)ret.value) == 2,
	      "illegal instruction count");
	pmu_stop_reset((unsigned long)ret.value);
	pmu_stop_reset(idx);
	CHECK_RET(sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ, idx),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call1(SBI_EXT_PMU, SBI_PMU_COUNTER_FW_READ, 0),
		  SBI_ERR_INVALID_PARAM);

	if (hw) {
		/*
		 * Cycles: on a counter we can read, running only when started.
		 */
		ret = pmu_config(0, 0x5, PMU_CFG_START, SBI_PMU_HW_CPU_CYCLES);
		CHECK_RET(ret, SBI_SUCCESS);
		CHECK(ret.value == 0, "cycles on counter %ld", ret.value);
		c0 = csr_read(cycle);
		for (int i = 0; i < 1000; i++)
			cpu_relax();
		c1 = csr_read(cycle);
		CHECK(c1 > c0, "cycle counter does not count");
		CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_COUNTER_STOP, 0, 1, 0),
			  SBI_SUCCESS);
		/* QEMU before 9.1 only notices the stop on the next read. */
		(void)csr_read(cycle);
		c0 = csr_read(cycle);
		for (int i = 0; i < 1000; i++)
			cpu_relax();
		CHECK(csr_read(cycle) == c0, "stopped cycle counter moved");
		/* Taken: a second request cannot have it. */
		CHECK_RET(pmu_config(0, 0x1, 0, SBI_PMU_HW_CPU_CYCLES),
			  SBI_ERR_NOT_SUPPORTED);
		pmu_stop_reset(0);
		c0 = csr_read(cycle);
		CHECK(csr_read(cycle) > c0,
		      "released cycle counter does not run");

		ret = pmu_config(0, 0x5, PMU_CFG_START,
				 SBI_PMU_HW_INSTRUCTIONS);
		CHECK_RET(ret, SBI_SUCCESS);
		CHECK(ret.value == 2, "instructions on counter %ld", ret.value);
		pmu_stop_reset(2);
	}

	CHECK_RET(pmu_config(fw_first, fw_mask, 0, FW_EVENT(1000)),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(pmu_config(0, 0, 0, SBI_PMU_HW_CPU_CYCLES),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(pmu_config(total, 1, 0, SBI_PMU_HW_CPU_CYCLES),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(pmu_config(fw_first, 1, SBI_PMU_CFG_FLAG_SKIP_MATCH,
			     FW_EVENT(SBI_PMU_FW_SET_TIMER)),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call(SBI_EXT_PMU, SBI_PMU_COUNTER_START, fw_first, 1, 0,
			   0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_PMU, SBI_PMU_SNAPSHOT_SET_SHMEM, 0, 0, 0),
		  SBI_ERR_NOT_SUPPORTED);

	/* event_get_info: which of these events can be counted at all? */
	static struct {
		uint32_t event_idx, output;
		uint64_t event_data;
	} __aligned(16) info[3];

	info[0].event_idx = SBI_PMU_HW_CPU_CYCLES;
	info[1].event_idx = FW_EVENT(SBI_PMU_FW_IPI_SENT);
	info[2].event_idx = FW_EVENT(1000);
	info[0].output = 0;
	info[1].output = 0;
	info[2].output = 1;
	ret = sbi_call(SBI_EXT_PMU, SBI_PMU_EVENT_GET_INFO, (unsigned long)info,
		       0, 3, 0, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK((info[0].output & 1) == (hw != 0), "cycles: output %x",
	      info[0].output);
	CHECK(info[1].output & 1, "firmware event not supported");
	CHECK(!(info[2].output & 1), "bogus firmware event supported");
	CHECK_RET(sbi_call(SBI_EXT_PMU, SBI_PMU_EVENT_GET_INFO,
			   (unsigned long)info + 4, 0, 1, 0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call(SBI_EXT_PMU, SBI_PMU_EVENT_GET_INFO,
			   CONFIG_MONITOR_LOAD_ADDR, 0, 1, 0, 0),
		  SBI_ERR_INVALID_ADDRESS);
}
#else
static void test_pmu(void)
{
}
#endif

static void test_fwft(void)
{
	static const char *const names[] = {
		"misaligned-deleg", "landing-pad", "shadow-stack",
		"double-trap",	    "pte-ad-hw",   "pointer-masking",
	};
	struct sbiret ret = {};

	printf("fwft\n ");
	for (unsigned long f = 0; f < ARRAY_SIZE(names); f++) {
		ret = sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET, f);
		CHECK(ret.error == SBI_SUCCESS ||
		      ret.error == SBI_ERR_NOT_SUPPORTED,
		      "%s: get error %ld", names[f], ret.error);
		if (ret.error)
			continue;
		printf(" %s=%ld", names[f], ret.value);
		/* Writing the current value back always works. */
		CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET, f,
				    (unsigned long)ret.value, 0),
			  SBI_SUCCESS);
		CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET, f, 1000, 0),
			  SBI_ERR_INVALID_PARAM);
		CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET, f, 0, 2),
			  SBI_ERR_INVALID_PARAM);
	}
	printf("\n");

	/* Misaligned delegation exists everywhere: toggle it, then lock it. */
	ret = sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			SBI_FWFT_MISALIGNED_EXC_DELEG);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 0, "misaligned exceptions delegated at entry");
	CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
			    SBI_FWFT_MISALIGNED_EXC_DELEG, 1, 0),
		  SBI_SUCCESS);
	ret = sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			SBI_FWFT_MISALIGNED_EXC_DELEG);
	CHECK(ret.value == 1, "delegation did not stick");
	CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
			    SBI_FWFT_MISALIGNED_EXC_DELEG, 0,
			    SBI_FWFT_SET_FLAG_LOCK),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call3(SBI_EXT_FWFT, SBI_FWFT_SET,
			    SBI_FWFT_MISALIGNED_EXC_DELEG, 1, 0),
		  SBI_ERR_DENIED_LOCKED);
	ret = sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			SBI_FWFT_MISALIGNED_EXC_DELEG);
	CHECK(ret.error == SBI_SUCCESS && ret.value == 0, "locked value %ld",
	      ret.value);

	CHECK_RET(sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			    SBI_FWFT_LOCAL_RESERVED_START),
		  SBI_ERR_DENIED);
	CHECK_RET(sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			    SBI_FWFT_GLOBAL_RESERVED_START),
		  SBI_ERR_DENIED);
	CHECK_RET(sbi_call1(SBI_EXT_FWFT, SBI_FWFT_GET,
			    SBI_FWFT_LOCAL_PLATFORM_START),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call0(SBI_EXT_FWFT, 2), SBI_ERR_NOT_SUPPORTED);
}

static void test_srst_errors(void)
{
	printf("srst\n");
	CHECK_RET(sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET, 3,
			    SBI_SRST_REASON_NONE),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET,
			    SBI_SRST_TYPE_SHUTDOWN, 2),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET, UL(0xf0000000),
			    SBI_SRST_REASON_NONE),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call0(SBI_EXT_SRST, 1), SBI_ERR_NOT_SUPPORTED);
}

static bool system_suspended;
static void test_finish(void);

#ifdef CONFIG_SBI_SUSP
/*
 * Every other hart is stopped by now. The call does not return: the timer
 * wakes us up in resume_main(), which goes on with test_finish().
 */
static void test_susp(void)
{
	uint64_t t0 = now();

	printf("susp\n");
	CHECK_RET(sbi_call3(SBI_EXT_SUSP, SBI_SUSP_SYSTEM_SUSPEND, 1,
			    (unsigned long)_resume_start, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_SUSP, SBI_SUSP_SYSTEM_SUSPEND,
			    UL(0x80000000), (unsigned long)_resume_start, 0),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call3(SBI_EXT_SUSP, SBI_SUSP_SYSTEM_SUSPEND, 0,
			    CONFIG_MONITOR_LOAD_ADDR, 0),
		  SBI_ERR_INVALID_ADDRESS);

	csr_write(sie, SIP_STIP);
	sbi_set_timer(t0 + TICKS_SHORT);
	system_suspended = true;
	CHECK_RET(sbi_call3(SBI_EXT_SUSP, SBI_SUSP_SYSTEM_SUSPEND,
			    SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM,
			    (unsigned long)_resume_start, MAGIC),
		  SBI_SUCCESS);
	/* Only reached when the suspend failed. */
	system_suspended = false;
	CHECK(false, "system suspend returned");
	sbi_set_timer(~ULL(0));
	csr_write(sie, 0);
}
#else
static void test_susp(void)
{
}
#endif

void test_main(unsigned long hartid, unsigned long fdt)
{
	boot_hartid = hartid;

	printf("\nsbitest: hart %lu, fdt %lx\n", hartid, fdt);
	CHECK(csr_read(satp) == 0, "satp %lx at entry", csr_read(satp));
	CHECK(!(csr_read(sstatus) & SSTATUS_SIE),
	      "interrupts enabled at entry");
	CHECK(csr_read(sie) == 0, "sie %lx at entry", csr_read(sie));

	test_fdt(fdt);
	test_base();
	test_dbcn();
	test_time();
	test_ipi_self();
	test_legacy();
	test_traps();
	test_pmu();
	test_fwft();
	test_smp();
	test_susp();
	test_finish();
}

static void system_resumed(unsigned long opaque)
{
	CHECK(system_suspended, "boot hart resumed without a system suspend");
	CHECK(opaque == MAGIC, "opaque %lx after system suspend", opaque);
	CHECK(csr_read(sip) & SIP_STIP,
	      "resumed without the wake-up interrupt pending");
	CHECK(csr_read(satp) == 0 && !(csr_read(sstatus) & SSTATUS_SIE),
	      "satp/sstatus.SIE not reset on resume");
	sbi_set_timer(~ULL(0));
	csr_write(sie, 0);
	test_finish();
}

static void test_finish(void)
{
	test_srst_errors();

	printf("sbitest: %u checks, %u failed\n", checks, failures);
	printf("sbitest: %s\n", failures ? "FAIL" : "PASS");

	/* The last check is whether this returns. */
	sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET, SBI_SRST_TYPE_SHUTDOWN,
		  failures ? SBI_SRST_REASON_SYSTEM_FAILURE :
		  SBI_SRST_REASON_NONE);
	printf("sbitest: system reset returned\nsbitest: FAIL\n");
}
