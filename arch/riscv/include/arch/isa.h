/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_ISA_H
#define ARCH_ISA_H

/*
 * What the device tree says the harts implement: "riscv,isa-extensions" of
 * the boot hart's cpu node, or the older "riscv,isa" string.
 *
 * The monitor finds out what it needs to know by looking: a CSR is there
 * or traps, a WARL bit sticks or does not. The tree gets a veto, though:
 * where it lists extensions at all, one it leaves out is not used even if
 * its registers seem to be there (isa_allows()), which is how a platform
 * keeps the monitor off something half implemented or not to be used. A
 * tree without the properties vetoes nothing.
 */

#include <stdbool.h>

/* Boot hart, with the heap up. */
void isa_init(const void *fdt);
/* Does the tree list extensions at all? */
bool isa_known(void);
/* Is 'ext' (lower case: "zkr", "smcdeleg", "v") among them? */
bool isa_has(const char *ext);

static inline bool isa_allows(const char *ext)
{
	return !isa_known() || isa_has(ext);
}

/* "rv64 i m a ... zicbom ...", for the boot log; NULL without a list. */
const char *isa_string(void);

#endif
