// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * CPPC through the platform microcontroller: RPMI CPPC service group, the
 * message-based services (no fast channels). RPMI and SBI number the CPPC
 * registers alike.
 */

#include <arch/hart.h>
#include <cppc.h>
#include <driver.h>
#include <rpmi.h>
#include <sbi/sbi.h>
#include <util.h>

static long rpmi_to_sbi(int rc)
{
	switch (rc) {
	case RPMI_SUCCESS:
		return SBI_SUCCESS;
	case RPMI_ERR_INVALID_PARAM:
		return SBI_ERR_INVALID_PARAM;
	case RPMI_ERR_NOT_SUPPORTED:
		return SBI_ERR_NOT_SUPPORTED;
	case RPMI_ERR_DENIED:
		return SBI_ERR_DENIED;
	default:
		return SBI_ERR_FAILED;
	}
}

static long rpmi_cppc_probe(uint32_t reg, uint32_t *width)
{
	uint32_t req[2] = { reg, (uint32_t)this_hartid() }, resp[2] = { 0 };
	int rc = rpmi_call(RPMI_GROUP_CPPC, RPMI_CPPC_PROBE_REG, req, 2, resp,
			   2);

	/* "Not there" is an answer to a probe, not a failure. */
	*width = rc ? 0 : resp[1];
	return rc == RPMI_ERR_NOT_SUPPORTED ? SBI_SUCCESS : rpmi_to_sbi(rc);
}

static long rpmi_cppc_read(uint32_t reg, uint64_t *val)
{
	uint32_t req[2] = { reg, (uint32_t)this_hartid() }, resp[3] = { 0 };
	int rc =
		rpmi_call(RPMI_GROUP_CPPC, RPMI_CPPC_READ_REG, req, 2, resp, 3);

	*val = reg_pair_to_64(resp[2], resp[1]);
	return rpmi_to_sbi(rc);
}

static long rpmi_cppc_write(uint32_t reg, uint64_t val)
{
	uint32_t req[4] = { reg, (uint32_t)this_hartid(), (uint32_t)val,
			    high32_from_64(val) };
	uint32_t resp[1] = {};

	return rpmi_to_sbi(rpmi_call(RPMI_GROUP_CPPC, RPMI_CPPC_WRITE_REG, req,
				     4, resp, 1));
}

static const struct cppc_ops rpmi_cppc_ops = {
	.name = "rpmi",
	.probe = rpmi_cppc_probe,
	.read = rpmi_cppc_read,
	.write = rpmi_cppc_write,
};

static int rpmi_cppc_probe_driver(const void *fdt)
{
	cppc_register(&rpmi_cppc_ops);
	return 0;
}

DRIVER_DEFINE(rpmi_cppc) = {
	.name = "rpmi-cppc",
	.probe = rpmi_cppc_probe_driver,
};
