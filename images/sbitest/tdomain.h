/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBITEST_TDOMAIN_H
#define SBITEST_TDOMAIN_H

#include <arch/csr.h>
#include <util.h>

/*
 * What the test payload (domain "untrusted") and the code it has running
 * in the domains "trusted" and "island" agree on. See the platform's
 * device tree (platform/qemu/virt/plat.c) for the domains themselves.
 */

/* The C side of tdomain_entry.S. */
void trusted_main(unsigned long hartid, unsigned long arg1);
void trusted_secondary_main(unsigned long hartid, unsigned long opaque);
void island_main(unsigned long hartid);

#define DOM_TRUSTED 1
#define DOM_UNTRUSTED 2
#define DOM_ISLAND 3

#define DOM_MEM_SIZE UL(0x100000)
#define DOM_TMEM ((unsigned long)CONFIG_QEMU_VIRT_DOMAINS_MEM)
#define DOM_IMEM (DOM_TMEM + DOM_MEM_SIZE)
#define DOM_SHARED ((struct dom_shared *)(DOM_TMEM + 2 * DOM_MEM_SIZE))
/* A byte of the shared page that nothing is kept in: for access probes. */
#define DOM_SHARED_PROBE ((unsigned long)DOM_SHARED + 0x800)

/* The page all three can read and write: READ_ONCE, WRITE_ONCE, or atomic. */
struct dom_shared {
	unsigned long island_hart; /* hart id + 1 */
	unsigned long island_boots;
	unsigned long beat; /* the island counts */
	unsigned long waiting; /* hart id + 1 inside TCMD_WAIT */
	unsigned long release;
	/* hart id + 1 that started in "trusted" */
	unsigned long tsec;
	unsigned long tsec_opaque;
	unsigned long mm_dropped; /* the late completion's status, + 1 */
};

/*
 * Management mode, hosted by "trusted" for "untrusted": the channels of
 * the platform's device tree, and the shared page for MM shared memory
 * (the input area, the output area; the rest of the page is taken).
 * The service: the input bytes in reverse order, each xor MM_XOR. An input
 * of MM_DROP_SIZE bytes is a request the server sits on until it is too
 * late, to see its completion refused.
 */
#define DOM_CHANNEL_MM UL(0x3000)
#define DOM_CHANNEL_REQFWD UL(0x3001)
#define MM_IN_OFFSET U(0x400)
#define MM_OUT_OFFSET U(0x600)
#define MM_AREA_SIZE U(0x200)
#define MM_XOR 0xa5
#define MM_DROP_SIZE U(1)
#define MM_TIMEOUT_US U(200000)
/*
 * What the server found right, in the value it exits with (served count in bits
 * 7:0).
 */
#define MM_SAW_MSI BIT(8) /* REQFWD_NEW_MESSAGE, signalled by its MSI */
#define MM_SAW_EVENT \
	BIT(9) /* ... and fetched, with the message's header in it */
#define MM_SAW_SSIP BIT(10) /* "riscv,wakeup-ssip" */
#define MM_SAW_PIECES \
	BIT(11) /* a message longer than the channel's MSG_MAX_LEN */
#define MM_SAW_OWN_CHANNELS \
	BIT(12) /* its REQUEST_FORWARD channel, and not the MM one */
#define MM_BAD BIT(15) /* something else was not as it should be */

/*
 * "trusted" exits with the verdict of its boot checks (0: all fine, else
 * a bit per failed check), then serves commands: the argument of an enter
 * is cmd | param << 8, the value of the exit that follows is the answer.
 */
#define TCMD_ECHO 1 /* 3 * param + 1 */
/*
 * f8/f9 and v8/v31 = param; bit 0/1: they were not zero before, bit 4/5:
 * there is an FPU / a vector unit.
 */
#define TCMD_UNITS_SET 2
#define TCMD_UNITS_GET 3 /* bit 0/1: they still hold param */
#define TCMD_WAIT 4 /* until dom_shared.release; TCMD_WAIT_DONE */
#define TCMD_START 5 /* sbi_hart_start(param) in "trusted": its error */

#define TCMD_SERVICES_SET 6 /* services_pristine() | services_mark() << 8 */
#define TCMD_SERVICES_GET 7 /* services_check() */
#define TCMD_MM_SERVE 9 /* serve param MM requests: count | MM_SAW_* */
#define TCMD_INSTRET 8 /* instret_coarse() over there */
#define TCMD_MPXY_SHMEM 13 /* 1: the hart came with no MPXY shared memory set */
#define TCMD_HYP_SET \
	10 /* hyp_set(param): bit 0: the CSRs were not as out of reset */
#define TCMD_HYP_GET 11 /* hyp_holds(param) */

/*
 * A hypervisor's CSRs (harts with the H extension): some of HS-level and
 * of VS-level, set to what 'x' makes of them.
 */
#define HYP_HEDELEG(x) ((x) & 1 ? BIT(CAUSE_BREAKPOINT) : BIT(CAUSE_USER_ECALL))

static inline unsigned long hyp_set(unsigned long x)
{
	unsigned long was = csr_read(CSR_VSSCRATCH) | csr_read(CSR_VSTVEC) |
			    csr_read(CSR_HEDELEG) | csr_read(CSR_HTIMEDELTA) |
			    csr_read(CSR_VSATP) | csr_read(CSR_HGATP);

	csr_write(CSR_VSSCRATCH, x);
	csr_write(CSR_VSTVEC, x << 2);
	csr_write(CSR_HEDELEG, HYP_HEDELEG(x));
	csr_write(CSR_HTIMEDELTA, ~x);
	return was != 0;
}

static inline bool hyp_holds(unsigned long x)
{
	return csr_read(CSR_VSSCRATCH) == x && csr_read(CSR_VSTVEC) == x << 2 &&
	       csr_read(CSR_HEDELEG) == HYP_HEDELEG(x) &&
	       csr_read(CSR_HTIMEDELTA) == ~x;
}

#define TCMD(cmd, param) ((cmd) | SHIFT_UL(param, 8))
#define TCMD_WAIT_DONE UL(0x3a17)
#define TSEC_OPAQUE UL(0x5ec)
#define TSEC_FIRST_EXIT UL(0x77)

#define SSTATUS_FS GENMASK_UL(14, 13)
#define SSTATUS_VS GENMASK_UL(10, 9)

/*
 * State a domain keeps with the monitor's per-hart services, for both
 * sides to leave there and look for again (tdomain.c; no variables, so
 * that the trusted domain can run it). 'who' tells the two apart, 'mem'
 * is a page of the caller's for the calls that need memory. All three
 * return a bit per thing found wrong.
 */
#define SERVICES_UNTRUSTED UL(1)
#define SERVICES_TRUSTED UL(2)
unsigned long services_pristine(unsigned long *mem);
unsigned long services_mark(unsigned long who, unsigned long *mem);
unsigned long services_check(unsigned long who, unsigned long *mem);
void services_clean(unsigned long *mem);
/*
 * Bits 16 to 31 of the instruction counter, for differences modulo 2^16:
 * enough to tell who counted, and one CSR on RV32 as well, where QEMU does
 * not carry from one half of a counter into the other.
 */
#define INSTRET_COARSE_MASK UL(0xffff)
unsigned long instret_coarse(void);

/* tdomain_entry.S */
long trusted_probe(unsigned long addr, unsigned long write);
void domain_enter_regs(unsigned long domain, unsigned long arg,
		       unsigned long *out);
long trusted_unit_probe(unsigned long vector);
void fp_set(unsigned long x);
unsigned long fp_get(void);
unsigned long fp_get_inv(void);
void vec_set(unsigned long x);
unsigned long vec_peek(void);
unsigned long vec_get(void);
unsigned long vec_get_inv(void);
unsigned long vec_vl(void);
void _trusted_secondary_start(void);

#endif
