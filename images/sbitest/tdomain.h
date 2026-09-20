/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBITEST_TDOMAIN_H
#define SBITEST_TDOMAIN_H

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
};

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

#define TCMD(cmd, param) ((cmd) | SHIFT_UL(param, 8))
#define TCMD_WAIT_DONE UL(0x3a17)
#define TSEC_OPAQUE UL(0x5ec)
#define TSEC_FIRST_EXIT UL(0x77)

#define SSTATUS_FS GENMASK_UL(14, 13)
#define SSTATUS_VS GENMASK_UL(10, 9)

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
