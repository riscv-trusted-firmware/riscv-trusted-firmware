// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * System reset through the platform microcontroller: RPMI SYSTEM_RESET
 * service group. The PuC is asked whether it supports a reset type when
 * that first matters (it may not be up while the monitor boots); an answer
 * is remembered, silence is not.
 */

#include <driver.h>
#include <reset.h>
#include <rpmi.h>

/* RPMI reset types 0, 1, 2 are enum reset_type. */
#define NR_TYPES 3

static enum { UNKNOWN, NO, YES } support[NR_TYPES];

static bool rpmi_reset_supported(enum reset_type type)
{
	uint32_t req = type, resp[2] = {};

	if (type >= NR_TYPES)
		return false;
	if (support[type] == UNKNOWN &&
	    !rpmi_call(RPMI_GROUP_SYSTEM_RESET, RPMI_SYSRST_GET_ATTRIBUTES,
		       &req, 1, resp, 2))
		support[type] = resp[1] & 1 ? YES : NO;
	return support[type] == YES;
}

static void rpmi_reset_request(enum reset_type type)
{
	uint32_t req = type;

	/* A posted request: the PuC resets the system under our feet. */
	rpmi_post(RPMI_GROUP_SYSTEM_RESET, RPMI_SYSRST_RESET, &req,
		  sizeof(req));
}

static const struct reset_ops rpmi_reset_ops = {
	.name = "rpmi",
	.rating = 200,
	.supported = rpmi_reset_supported,
	.reset = rpmi_reset_request,
};

static int rpmi_reset_probe(const void *fdt)
{
	/*
	 * Whatever the probe order: without a transport nothing is supported.
	 */
	reset_register(&rpmi_reset_ops);
	return 0;
}

DRIVER_DEFINE(rpmi_reset) = {
	.name = "rpmi-reset",
	.probe = rpmi_reset_probe,
};
