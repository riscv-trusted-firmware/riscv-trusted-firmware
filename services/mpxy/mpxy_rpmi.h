/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef MPXY_RPMI_H
#define MPXY_RPMI_H

/*
 * What the monitor knows of the RPMI service groups it proxies: which
 * services a group has and how long their requests are, so that what
 * reaches the PuC is at least a well-formed request of a service that
 * exists; and for a group where a request can be the monitor's business
 * (SYSTEM_MSI), a say in it. A group without rules here (an experimental
 * or implementation specific one) is passed on as it comes.
 */

#include <rpmi.h>
#include <stddef.h>
#include <stdint.h>
#include <util.h>

/* The RPMI message protocol attributes of a channel. */
#define MPXY_RPMI_ATTR_SERVICEGROUP_ID U(0x80000000)
#define MPXY_RPMI_ATTR_SERVICEGROUP_VERSION U(0x80000001)
#define MPXY_RPMI_ATTR_IMPL_ID U(0x80000002)
#define MPXY_RPMI_ATTR_IMPL_VERSION U(0x80000003)

/* Request data of any length the transport takes. */
#define RPMI_RULE_ANY_LEN 0xffff

struct rpmi_service_rule {
	uint8_t service;
	uint16_t req_min, req_max; /* request data, bytes */
};

struct rpmi_group_state;

struct rpmi_group_rules {
	uint16_t group;
	const struct rpmi_service_rule *services;
	unsigned int nr_services;
	/*
	 * Once per channel, when the PuC is first talked to: 0, or an RPMI
	 * error when the group is not usable after all.
	 */
	int (*setup)(struct rpmi_context *puc, struct rpmi_group_state *state);
	/*
	 * A request that passed the rules: 0 to send it on, or the RPMI status
	 * (negative) the monitor answers it with itself.
	 */
	int (*filter)(const struct rpmi_group_state *state, uint8_t service,
		      const uint32_t *req);
};

/* What a group's setup() and filter() keep per channel. */
struct rpmi_group_state {
	uint32_t num_msi;
	/* System MSIs that are not S-mode's: bit per index. */
	unsigned long msi_denied[(CONFIG_MPXY_RPMI_MAX_SYSMSI +
				  8 * sizeof(long) - 1) /
				 (8 * sizeof(long))];
};

/* NULL: no rules for this group. */
const struct rpmi_group_rules *rpmi_group_rules_find(uint16_t group);
const struct rpmi_service_rule *
rpmi_service_rule_find(const struct rpmi_group_rules *rules, uint8_t service);

#endif
