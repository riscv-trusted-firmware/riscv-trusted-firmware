// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI debug console extension (EID "DBCN"). Buffers are named by physical
 * address, so no translation is involved; they only have to lie outside
 * the monitor's own memory.
 */

#include <arch/hart.h>
#include <arch/pmp.h>
#include <console.h>

#include "sbi_internal.h"

static bool dbcn_buffer_ok(unsigned long len, unsigned long lo,
			   unsigned long hi)
{
	/* No physical address above XLEN bits is reachable from M-mode. */
	return hi == 0 && smode_range_ok(lo, len);
}

static struct service_ret sbi_dbcn_ecall(unsigned long eid, unsigned long fid,
					 struct trap_regs *regs)
{
	unsigned long len = regs->a0;

	switch (fid) {
	case SBI_DBCN_CONSOLE_WRITE:
		/* Read-only memory will do for what is only read. */
		if (regs->a2 || !smode_range_readable(regs->a1, len))
			return sbi_err(SBI_ERR_INVALID_PARAM);
		console_write(smode_access_begin(regs->a1, len), len);
		smode_access_end();
		return sbi_ok((long)len);
	case SBI_DBCN_CONSOLE_READ:
		if (!dbcn_buffer_ok(len, regs->a1, regs->a2))
			return sbi_err(SBI_ERR_INVALID_PARAM);
		len = console_read(smode_access_begin(regs->a1, len), len);
		smode_access_end();
		return sbi_ok((long)len);
	case SBI_DBCN_CONSOLE_WRITE_BYTE:
		console_write(&(char){ (char)regs->a0 }, 1);
		return sbi_ok(0);
	default:
		return sbi_err(SBI_ERR_NOT_SUPPORTED);
	}
}

SERVICE_DEFINE(sbi_dbcn) = {
	.name = "sbi-dbcn",
	.eid_min = SBI_EXT_DBCN,
	.eid_max = SBI_EXT_DBCN,
	.ecall = sbi_dbcn_ecall,
};
