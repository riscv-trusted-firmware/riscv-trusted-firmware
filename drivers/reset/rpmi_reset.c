// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * System reset through the platform microcontroller: RPMI SYSTEM_RESET
 * service group ("riscv,rpmi-system-reset"). The PuC is asked whether it
 * supports a reset type when that first matters (it may not be up while the
 * monitor boots); an answer is remembered, silence is not.
 */

#include <driver.h>
#include <reset.h>
#include <rpmi.h>

/* The PuC behind the node's "mboxes"; NULL until a node was probed. */
static struct rpmi_context *puc;

/* RPMI reset types 0, 1, 2 are enum reset_type. */
#define NR_TYPES 3

static enum { UNKNOWN, NO, YES } support[NR_TYPES];

static bool rpmi_reset_supported(enum reset_type type)
{
	uint32_t req = type, resp[2] = {};

	if (type >= NR_TYPES)
		return false;
	if (support[type] == UNKNOWN &&
	    !rpmi_call(puc, RPMI_GROUP_SYSTEM_RESET, RPMI_SYSRST_GET_ATTRIBUTES,
		       &req, 1, resp, 2))
		support[type] = resp[1] & 1 ? YES : NO;
	return support[type] == YES;
}

static void rpmi_reset_request(enum reset_type type)
{
	uint32_t req = type;

	/* A posted request: the PuC resets the system under our feet. */
	rpmi_post(puc, RPMI_GROUP_SYSTEM_RESET, RPMI_SYSRST_RESET, &req,
		  sizeof(req));
}

static const struct reset_ops rpmi_reset_ops = {
	.name = "rpmi",
	.rating = 200,
	.supported = rpmi_reset_supported,
	.reset = rpmi_reset_request,
};

static int rpmi_reset_probe(const void *fdt, int node)
{
	uint16_t group = 0;

	/*
	 * "mboxes = <&transport group>": which PuC, and a check on the group.
	 */
	if (node < 0 || puc)
		return 0;
	if (rpmi_client_from_fdt(fdt, node, &puc, &group) ||
	    group != RPMI_GROUP_SYSTEM_RESET) {
		puc = NULL;
		return -1;
	}
	reset_register(&rpmi_reset_ops);
	return 0;
}

static const char *const rpmi_reset_compatible[] = { "riscv,rpmi-system-reset",
						     NULL };

DRIVER_DEFINE(rpmi_reset) = {
	.name = "rpmi-system-reset",
	.compatible = rpmi_reset_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = rpmi_reset_probe,
};
