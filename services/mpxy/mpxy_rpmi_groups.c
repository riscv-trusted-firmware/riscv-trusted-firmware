// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * The RPMI service groups S-mode reaches through MPXY channels, as far as
 * the monitor has to know them: see mpxy_rpmi.h. Request lengths are the
 * ones of the RPMI specification v1.0; responses are the PuC's to get
 * right, and an error response is shorter than a good one anyway.
 */

#include <arch/hart.h>
#include <util.h>

#include "mpxy_rpmi.h"

#define ANY RPMI_RULE_ANY_LEN

/* ENABLE_NOTIFICATION is service 1 of every group: EVENT_ID, REQ_STATE. */
#define NOTIFY                                         \
	{                                              \
		RPMI_SERVICE_ENABLE_NOTIFICATION, 8, 8 \
	}

static const struct rpmi_service_rule sysmsi_services[] = {
	NOTIFY,
	{ RPMI_SYSMSI_GET_ATTRIBUTES, 0, 0 },
	{ RPMI_SYSMSI_GET_MSI_ATTRIBUTES, 4, 4 },
	{ RPMI_SYSMSI_SET_MSI_STATE, 8, 8 },
	{ RPMI_SYSMSI_GET_MSI_STATE, 4, 4 },
	{ RPMI_SYSMSI_SET_MSI_TARGET, 16, 16 },
	{ RPMI_SYSMSI_GET_MSI_TARGET, 4, 4 },
};

static const struct rpmi_service_rule voltage_services[] = {
	NOTIFY,		{ 0x02, 0, 0 }, /* GET_NUM_DOMAINS */
	{ 0x03, 4, 4 }, /* GET_ATTRIBUTES */
	{ 0x04, 8, 8 }, /* GET_SUPPORTED_LEVELS */
	{ 0x05, 8, 8 }, /* SET_CONFIG */
	{ 0x06, 4, 4 }, /* GET_CONFIG */
	{ 0x07, 8, 8 }, /* SET_LEVEL */
	{ 0x08, 4, 4 }, /* GET_LEVEL */
};

static const struct rpmi_service_rule clock_services[] = {
	NOTIFY,		  { 0x02, 0, 0 }, /* GET_NUM_CLOCKS */
	{ 0x03, 4, 4 }, /* GET_ATTRIBUTES */
	{ 0x04, 8, 8 }, /* GET_SUPPORTED_RATES */
	{ 0x05, 8, 8 }, /* SET_CONFIG */
	{ 0x06, 4, 4 }, /* GET_CONFIG */
	{ 0x07, 16, 16 }, /* SET_RATE */
	{ 0x08, 4, 4 }, /* GET_RATE */
};

static const struct rpmi_service_rule device_power_services[] = {
	NOTIFY,		{ 0x02, 0, 0 }, /* GET_NUM_DOMAINS */
	{ 0x03, 4, 4 }, /* GET_ATTRIBUTES */
	{ 0x04, 8, 8 }, /* SET_STATE */
	{ 0x05, 4, 4 }, /* GET_STATE */
};

static const struct rpmi_service_rule performance_services[] = {
	NOTIFY,		  { 0x02, 0, 0 }, /* GET_NUM_DOMAINS */
	{ 0x03, 4, 4 }, /* GET_ATTRIBUTES */
	{ 0x04, 8, 8 }, /* GET_SUPPORTED_LEVELS */
	{ 0x05, 4, 4 }, /* GET_LEVEL */
	{ 0x06, 8, 8 }, /* SET_LEVEL */
	{ 0x07, 4, 4 }, /* GET_LIMIT */
	{ 0x08, 12, 12 }, /* SET_LIMIT */
	{ 0x09, 0, 0 }, /* GET_FAST_CHANNEL_REGION */
	{ 0x0a, 8, 8 }, /* GET_FAST_CHANNEL_ATTRIBUTES */
};

static const struct rpmi_service_rule management_mode_services[] = {
	NOTIFY,
	{ 0x02, 0, 0 }, /* GET_ATTRIBUTES */
	{ 0x03, 16, 16 }, /* COMMUNICATE */
};

static const struct rpmi_service_rule ras_agent_services[] = {
	NOTIFY,
	{ 0x02, 0, 0 }, /* GET_NUM_ERR_SRCS */
	{ 0x03, 4, 4 }, /* GET_ERR_SRCS_ID_LIST */
	{ 0x04, 8, 8 }, /* GET_ERR_SRC_DESC */
};

static const struct rpmi_service_rule request_forward_services[] = {
	NOTIFY,
	{ 0x02, 4, 4 }, /* RETRIEVE_CURRENT_MESSAGE */
	/* COMPLETE_CURRENT_MESSAGE: the response data */
	{ 0x03, 0, ANY },
};

/*
 * SYSTEM_MSI. The PuC's system MSIs are for whoever handles them, and some
 * are not S-mode's: the ones the PuC says M-mode should handle, and the one
 * that is the transport's P2A doorbell. Those read as denied. And an MSI is
 * a write to an address of the target's choosing, done by the PuC: it has
 * to be an address S-mode could write itself, not the monitor's interrupt
 * files, say.
 */
static bool msi_denied(const struct rpmi_group_state *state, uint32_t index)
{
	if (index >= CONFIG_MPXY_RPMI_MAX_SYSMSI)
		return true;
	return (state->msi_denied[index / (8 * sizeof(long))] >>
		(index % (8 * sizeof(long)))) &
	       1;
}

static int sysmsi_setup(struct rpmi_context *puc,
			struct rpmi_group_state *state)
{
	uint32_t resp[7] = {}, index = 0;
	int rc = 0;

	rc = rpmi_call(puc, RPMI_GROUP_SYSTEM_MSI, RPMI_SYSMSI_GET_ATTRIBUTES,
		       NULL, 0, resp, 4);
	if (rc)
		return rc;
	state->num_msi = resp[1];
	for (index = 0;
	     index < state->num_msi && index < CONFIG_MPXY_RPMI_MAX_SYSMSI;
	     index++) {
		rc = rpmi_call(puc, RPMI_GROUP_SYSTEM_MSI,
			       RPMI_SYSMSI_GET_MSI_ATTRIBUTES, &index, 1, resp,
			       7);
		if (rc)
			return rc;
		if ((resp[1] & RPMI_SYSMSI_FLAGS0_PREF_MMODE) ||
		    index == puc->p2a_doorbell_sysmsi)
			state->msi_denied[index / (8 * sizeof(long))] |=
				BIT(index % (8 * sizeof(long)));
	}
	return 0;
}

static int sysmsi_filter(const struct rpmi_group_state *state, uint8_t service,
			 const uint32_t *req)
{
	if (service < RPMI_SYSMSI_GET_MSI_ATTRIBUTES)
		return 0;
	/* All the others are about one MSI, the index first. */
	if (req[0] >= state->num_msi)
		return RPMI_ERR_INVALID_PARAM;
	if (msi_denied(state, req[0]))
		return RPMI_ERR_DENIED;
	if (service == RPMI_SYSMSI_SET_MSI_TARGET) {
		uint64_t addr = reg_pair_to_64(req[2], req[1]);

		if (addr != (unsigned long)addr ||
		    !smode_range_ok((unsigned long)addr, 4))
			return RPMI_ERR_INVALID_ADDR;
	}
	return 0;
}

static const struct rpmi_group_rules groups[] = {
	{
		.group = RPMI_GROUP_SYSTEM_MSI,
		.services = sysmsi_services,
		.nr_services = ARRAY_SIZE(sysmsi_services),
		.setup = sysmsi_setup,
		.filter = sysmsi_filter,
	},
	{
		.group = RPMI_GROUP_VOLTAGE,
		.services = voltage_services,
		.nr_services = ARRAY_SIZE(voltage_services),
	},
	{
		.group = RPMI_GROUP_CLOCK,
		.services = clock_services,
		.nr_services = ARRAY_SIZE(clock_services),
	},
	{
		.group = RPMI_GROUP_DEVICE_POWER,
		.services = device_power_services,
		.nr_services = ARRAY_SIZE(device_power_services),
	},
	{
		.group = RPMI_GROUP_PERFORMANCE,
		.services = performance_services,
		.nr_services = ARRAY_SIZE(performance_services),
	},
	{
		.group = RPMI_GROUP_MANAGEMENT_MODE,
		.services = management_mode_services,
		.nr_services = ARRAY_SIZE(management_mode_services),
	},
	{
		.group = RPMI_GROUP_RAS_AGENT,
		.services = ras_agent_services,
		.nr_services = ARRAY_SIZE(ras_agent_services),
	},
	{
		.group = RPMI_GROUP_REQUEST_FORWARD,
		.services = request_forward_services,
		.nr_services = ARRAY_SIZE(request_forward_services),
	},
};

const struct rpmi_group_rules *rpmi_group_rules_find(uint16_t group)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(groups); i++)
		if (groups[i].group == group)
			return &groups[i];
	return NULL;
}

const struct rpmi_service_rule *
rpmi_service_rule_find(const struct rpmi_group_rules *rules, uint8_t service)
{
	for (unsigned int i = 0; i < rules->nr_services; i++)
		if (rules->services[i].service == service)
			return &rules->services[i];
	return NULL;
}
