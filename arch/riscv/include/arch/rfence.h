/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_RFENCE_H
#define ARCH_RFENCE_H

/* Remote fences: instruction cache and TLB shootdown across harts. */

#include <hartmask.h>

enum rfence_type {
	RFENCE_FENCE_I,
	RFENCE_SFENCE_VMA,
	RFENCE_SFENCE_VMA_ASID,
	RFENCE_HFENCE_GVMA,
	RFENCE_HFENCE_GVMA_VMID,
	RFENCE_HFENCE_VVMA,
	RFENCE_HFENCE_VVMA_ASID,
};

struct rfence_req {
	enum rfence_type type;
	unsigned long start;
	unsigned long size;
	unsigned long asid; /* ASID or VMID, for the types that take one */
};

/*
 * Run 'req' on every hart of 'targets' (the caller included, if set) and
 * return once all of them are done. 0 or an SBI error code.
 */
int rfence_request(const struct hartmask *targets,
		   const struct rfence_req *req);

/* IPI_EVENT_RFENCE handler. */
void rfence_process(void);
/* Boot hart, once the hart table is there. */
void rfence_init(void);

#endif
