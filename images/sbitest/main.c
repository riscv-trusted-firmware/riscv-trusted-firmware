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

/* QEMU's mtime ticks at 10 MHz; only the order of magnitude matters. */
#define TICKS_SHORT ULL(20000)
#define TICKS_TIMEOUT ULL(20000000)

#define MAX_HARTS CONFIG_PLATFORM_HART_COUNT
#define BOGUS_EID UL(0x0badc0de)
#define MAGIC UL(0x5b17e57)

/* ---- bookkeeping ------------------------------------------------------ */

static unsigned int checks, failures;

#define CHECK(cond, fmt, ...)                                                 \
	do {                                                                  \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                           \
			printf("  FAIL %s:%d: " fmt "\n", __func__, __LINE__, \
			       ##__VA_ARGS__);                                \
		}                                                             \
	} while (0)

/* CHECK() of an SBI return's error, from where the macro is used. */
static void check_ret(const char *func, int line, long error, long expected)
{
	checks++;
	if (error != expected) {
		failures++;
		printf("  FAIL %s:%d: error %ld, expected %ld\n", func, line,
		       error, expected);
	}
}

#define CHECK_RET(ret, err) check_ret(__func__, __LINE__, (ret).error, err)

static uint64_t now(void)
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
};

struct mailbox {
	unsigned long cmd;
	unsigned long started; /* opaque seen by _secondary_start */
	unsigned long resumed; /* opaque seen by _resume_start */
	unsigned long ipis;
	unsigned long suspend_ret; /* retentive suspend returned */
	long suspend_error;
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

		switch (READ_ONCE(m->cmd)) {
		case CMD_STOP:
			WRITE_ONCE(m->cmd, CMD_NONE);
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

void resume_main(unsigned long hartid, unsigned long opaque)
{
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
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_PMU);
	CHECK(ret.value == 0, "PMU probed but not implemented");

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
	test_smp();
	test_srst_errors();

	printf("sbitest: %u checks, %u failed\n", checks, failures);
	printf("sbitest: %s\n", failures ? "FAIL" : "PASS");

	/* The last check is whether this returns. */
	sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET, SBI_SRST_TYPE_SHUTDOWN,
		  failures ? SBI_SRST_REASON_SYSTEM_FAILURE :
		  SBI_SRST_REASON_NONE);
	printf("sbitest: system reset returned\nsbitest: FAIL\n");
}
