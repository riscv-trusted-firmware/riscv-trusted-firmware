// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The code of the domains "trusted" and "island". It runs from this image,
 * which those domains can read and execute but not write: no variable of
 * the payload is theirs to use, no printf(), only the stack, their own
 * memory and the shared page.
 */

#include <arch/csr.h>
#include <atomic.h>
#include <io.h>
#include <stdbool.h>
#include <util.h>

#include "sbicall.h"
#include "tdomain.h"

static void say(const char *s)
{
	while (*s)
		sbi_call1(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE_BYTE,
			  (unsigned char)*s++);
}

static bool unit_on(unsigned long field)
{
	csr_set(sstatus, field);
	return !trusted_unit_probe(field == SSTATUS_VS);
}

static unsigned long units_set(unsigned long x)
{
	unsigned long v = 0;

	if (unit_on(SSTATUS_FS)) {
		v |= BIT(4) | (fp_get() || fp_get_inv() != ~UL(0) ? BIT(0) : 0);
		fp_set(x);
	}
	if (unit_on(SSTATUS_VS)) {
		v |= BIT(5) | (vec_peek() ? BIT(1) : 0);
		vec_set(x);
	}
	return v;
}

static unsigned long units_get(unsigned long x)
{
	unsigned long v = 0;

	if (unit_on(SSTATUS_FS) && fp_get() == x && fp_get_inv() == x)
		v |= BIT(0);
	/* Cleared rather than kept: vtype.vill, which vl = 0 gives away. */
	if (unit_on(SSTATUS_VS) && vec_vl() && vec_get() == x &&
	    vec_get_inv() == x)
		v |= BIT(1);
	return v;
}

/* Exit with 'value', and again with the answer to every command that comes. */
static void __noreturn trusted_serve(unsigned long hartid, unsigned long value)
{
	for (;;) {
		struct sbiret ret =
			sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_EXIT, value);
		unsigned long param = (unsigned long)ret.value >> 8;

		/*
		 * An exit that fails has nowhere to go: say so the only way
		 * left.
		 */
		if (ret.error) {
			say("  trusted: exit failed\n");
			value = ~UL(0);
			continue;
		}
		switch (ret.value & 0xff) {
		case TCMD_ECHO:
			value = 3 * param + 1;
			break;
		case TCMD_UNITS_SET:
			value = units_set(param);
			break;
		case TCMD_UNITS_GET:
			value = units_get(param);
			break;
		case TCMD_WAIT:
			WRITE_ONCE(DOM_SHARED->waiting, hartid + 1);
			while (!READ_ONCE(DOM_SHARED->release))
				;
			WRITE_ONCE(DOM_SHARED->waiting, 0);
			value = TCMD_WAIT_DONE;
			break;
		case TCMD_START:
			started = sbi_call3(SBI_EXT_HSM, SBI_HSM_HART_START,
					    param,
					    CONFIG_SBITEST_LOAD_ADDR + 12,
					    TSEC_OPAQUE);
			value = (unsigned long)started.error;
			break;
		default:
			value = ~UL(0);
			break;
		}
	}
}

void trusted_main(unsigned long hartid, unsigned long arg1)
{
	unsigned long *mine = (unsigned long *)DOM_TMEM;
	unsigned long fail = 0;

	say("  trusted: booting\n");
	if (csr_read(satp) || csr_read(sie))
		fail |= BIT(0);
	if (csr_read(sstatus) & (BIT(1) | SSTATUS_FS | SSTATUS_VS))
		fail |= BIT(1);
	WRITE_ONCE(*mine, 0x7e57 + hartid);
	if (READ_ONCE(*mine) != 0x7e57 + hartid)
		fail |= BIT(2);
	if (sbi_call0(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_SELF).value !=
	    DOM_TRUSTED)
		fail |= BIT(3);
	/* Not the system's master: no reset, no say over other domains. */
	if (sbi_call2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET,
		      SBI_SRST_TYPE_SHUTDOWN, 0)
		    .error != SBI_ERR_DENIED)
		fail |= BIT(4);
	if (sbi_call1(SBI_EXT_FW_DOMAIN, SBI_FW_DOMAIN_STOP, DOM_UNTRUSTED)
	    .error != SBI_ERR_DENIED)
		fail |= BIT(5);
	/* Its memory and the shared page, the image read-only, and no more. */
	if (trusted_probe(DOM_IMEM, 0) != CAUSE_LOAD_ACCESS)
		fail |= BIT(6);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR + CONFIG_SBITEST_SIZE, 0) !=
	    CAUSE_LOAD_ACCESS)
		fail |= BIT(7);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR, 1) != CAUSE_STORE_ACCESS)
		fail |= BIT(8);
	if (trusted_probe(CONFIG_SBITEST_LOAD_ADDR, 0) ||
	    trusted_probe(DOM_SHARED_PROBE, 1))
		fail |= BIT(9);
	/* Nor can it hand the monitor an address that is not its own. */
	if (sbi_call3(SBI_EXT_DBCN, SBI_DBCN_CONSOLE_WRITE, 1, DOM_IMEM, 0)
	    .error != SBI_ERR_INVALID_PARAM)
		fail |= BIT(10);
	/* The island's hart is not one it can see. */
	if (READ_ONCE(DOM_SHARED->island_hart) &&
	    sbi_call1(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS,
		      READ_ONCE(DOM_SHARED->island_hart) - 1)
			    .error != SBI_ERR_INVALID_PARAM)
		fail |= BIT(11);
	trusted_serve(hartid, fail);
}

void trusted_secondary_main(unsigned long hartid, unsigned long opaque)
{
	WRITE_ONCE(DOM_SHARED->tsec_opaque, opaque);
	WRITE_ONCE(DOM_SHARED->tsec, hartid + 1);
	trusted_serve(hartid, TSEC_FIRST_EXIT);
}

void island_main(unsigned long hartid)
{
	WRITE_ONCE(DOM_SHARED->island_hart, hartid + 1);
	atomic_add_ulong(&DOM_SHARED->island_boots, 1);
	for (;;)
		atomic_add_ulong(&DOM_SHARED->beat, 1);
}
