// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * System suspend through the platform microcontroller: RPMI SYSTEM_SUSPEND
 * service group. Once the PuC has acknowledged SYSSUSP_SUSPEND it takes the
 * system down when the calling hart executes WFI, which the caller of
 * suspend_prepare() does next.
 */

#include <arch/hart.h>
#include <driver.h>
#include <rpmi.h>
#include <sbi/sbi.h>
#include <suspend.h>

static bool rpmi_suspend_supported(uint32_t sleep_type)
{
	uint32_t req = sleep_type, resp[2] = {};

	/* Asked every time: a PuC that was silent may have come up since. */
	return !rpmi_call(RPMI_GROUP_SYSTEM_SUSPEND,
			  RPMI_SYSSUSP_GET_ATTRIBUTES, &req, 1, resp, 2) &&
	       (resp[1] & 1);
}

static long rpmi_suspend_prepare(uint32_t sleep_type, unsigned long resume_addr)
{
	uint32_t req[4] = {
		(uint32_t)this_hartid(),
		sleep_type,
		(uint32_t)resume_addr,
		(uint32_t)((uint64_t)resume_addr >> 32),
	};
	uint32_t resp[1] = {};

	return rpmi_call(RPMI_GROUP_SYSTEM_SUSPEND, RPMI_SYSSUSP_SUSPEND, req,
			 4, resp, 1) ?
		       SBI_ERR_FAILED :
		       SBI_SUCCESS;
}

static const struct suspend_ops rpmi_suspend_ops = {
	.name = "rpmi",
	.supported = rpmi_suspend_supported,
	.prepare = rpmi_suspend_prepare,
};

static int rpmi_suspend_probe(const void *fdt)
{
	/*
	 * Whatever the probe order: without a transport nothing is supported.
	 */
	suspend_register(&rpmi_suspend_ops);
	return 0;
}

DRIVER_DEFINE(rpmi_suspend) = {
	.name = "rpmi-suspend",
	.probe = rpmi_suspend_probe,
};
