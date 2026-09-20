/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SERVICE_H
#define SERVICE_H

/*
 * Services are the units of functionality the monitor exposes to lower
 * privilege levels: SBI extensions today, TEE / confidential-computing /
 * attestation dispatchers later. A service is a static descriptor placed in
 * the .service_table linker set by SERVICE_DEFINE(); no registration code.
 *
 * Ecall routing follows the SBI calling convention: a7 = extension ID,
 * a6 = function ID, a0..a5 = arguments, (a0, a1) = (error, value) on return.
 * A service owns a contiguous EID range.
 */

#include <arch/trap.h>
#include <compiler.h>
#include <linker_table.h>
#include <util.h>

struct service_ret {
	long error;
	long value;
};

/* Only a0 = error is returned, a1 is preserved (SBI v0.1 calls). */
#define SERVICE_LEGACY_RET BIT(0)

struct service {
	const char *name;
	unsigned long eid_min;
	unsigned long eid_max;
	unsigned int flags;
	/* Boot hart, once, before the platform's plat_init(). May be NULL. */
	int (*init)(void);
	/*
	 * Is 'eid' usable on this platform (its backend is there)? NULL means
	 * yes. The value is what the SBI probe call reports: 0 = no.
	 */
	long (*probe)(unsigned long eid);
	/*
	 * Called with the trapping hart's register file, mepc already past
	 * the ecall; may modify it, and may not return (hart stop).
	 */
	struct service_ret (*ecall)(unsigned long eid, unsigned long fid,
				    struct trap_regs *regs);
};

#define SERVICE_DEFINE(_sym) \
	static const struct service _sym __used __section(".service_table")

/* Provided by the linker script. */
extern const struct service __service_table_start[];
extern const struct service __service_table_end[];

#define for_each_service(s) LINKER_TABLE_FOREACH(s, service)

void services_init(void);
const struct service *service_lookup(unsigned long eid);
/* 0 when no service implements 'eid' on this platform. */
long service_probe(unsigned long eid);
/* Dispatch the ecall in 'regs' and store the result in it. */
void service_ecall(struct trap_regs *regs);

#endif
