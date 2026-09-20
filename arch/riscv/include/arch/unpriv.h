/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_UNPRIV_H
#define ARCH_UNPRIV_H

/*
 * Access memory the way the trapping context would: with its privilege
 * level and address translation (mstatus.MPRV). A fault does not reach the
 * trapping context; it is reported to the caller, which usually forwards
 * it with trap_redirect().
 */

#include <arch/trap.h>

/* Read a 1/2/4/8-byte value (8 on RV64 only). false: faulted, *fault is set. */
bool unpriv_read(const struct trap_regs *regs, unsigned long addr,
		 unsigned int width, unsigned long *val,
		 struct trap_info *fault);

bool unpriv_write_byte(const struct trap_regs *regs, unsigned long addr,
		       uint8_t val, struct trap_info *fault);

/* Fetch the instruction at regs->mepc (16 or 32 bits wide). */
bool unpriv_fetch_insn(const struct trap_regs *regs, unsigned long *insn,
		       struct trap_info *fault);

#endif
