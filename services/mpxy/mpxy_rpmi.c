// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI message protocol for MPXY channels ("Integration with SBI MPXY
 * Extension" of the RPMI specification): one channel per service group,
 * message_id = SERVICE_ID, message data = RPMI request / acknowledgment
 * data, notification events as the PuC packed them.
 *
 * Channels are "riscv,rpmi-mpxy-*" nodes of the device tree.
 *
 * What the PuC says about itself and the group (versions, implementation
 * id) is asked on first use rather than at boot, when the PuC may not be
 * up yet, and then cached.
 */

#include <driver.h>
#include <fdt_util.h>
#include <mpxy.h>
#include <rpmi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <string.h>
#include <util.h>

#include "mpxy_rpmi.h"

#define MPXY_RPMI_ATTR_SERVICEGROUP_ID U(0x80000000)
#define MPXY_RPMI_ATTR_SERVICEGROUP_VERSION U(0x80000001)
#define MPXY_RPMI_ATTR_IMPL_ID U(0x80000002)
#define MPXY_RPMI_ATTR_IMPL_VERSION U(0x80000003)
#define EVENT_BUF_SIZE CONFIG_MPXY_RPMI_EVENT_BUF_SIZE

struct mpxy_rpmi {
	struct mpxy_channel ch;
	struct rpmi_context *puc;
	uint16_t group;
	bool probed;
	uint32_t group_version, impl_id, impl_version;
	/*
	 * NULL for a group the monitor knows nothing of: passed on as it comes.
	 */
	const struct rpmi_group_rules *rules;
	struct rpmi_group_state state;

	/* Whole events, back to back. */
	unsigned long lock;
	uint32_t events[EVENT_BUF_SIZE / 4];
	size_t used;
	uint32_t count, lost;
};

static struct mpxy_rpmi pool[CONFIG_MPXY_RPMI_MAX_CHANNELS];
static unsigned int pool_used;

static struct mpxy_rpmi *to_rpmi(struct mpxy_channel *ch)
{
	return (struct mpxy_rpmi *)ch;
}

static long rpmi_to_sbi(int rc)
{
	switch (rc) {
	case RPMI_SUCCESS:
		return SBI_SUCCESS;
	case RPMI_ERR_TIMEOUT:
		return SBI_ERR_TIMEOUT;
	case RPMI_ERR_IO:
		return SBI_ERR_IO;
	case RPMI_ERR_INVALID_PARAM:
		return SBI_ERR_INVALID_PARAM;
	case RPMI_ERR_NOT_SUPPORTED:
		return SBI_ERR_NOT_SUPPORTED;
	case RPMI_ERR_DENIED:
		return SBI_ERR_DENIED;
	case RPMI_ERR_INVALID_ADDR:
		return SBI_ERR_INVALID_ADDRESS;
	default:
		return SBI_ERR_FAILED;
	}
}

static long mpxy_rpmi_ask_puc(struct mpxy_rpmi *r)
{
	uint32_t spec = 0, version = 0, impl_id = 0, impl_version = 0;
	int rc = 0;

	if (r->probed)
		return SBI_SUCCESS;

	rc = rpmi_base_get(r->puc, RPMI_BASE_GET_SPEC_VERSION, &spec);
	if (!rc)
		rc = rpmi_probe_group(r->puc, r->group, &version);
	if (!rc && !version)
		rc = RPMI_ERR_NOT_SUPPORTED;
	if (!rc)
		rc = rpmi_base_get(r->puc, RPMI_BASE_GET_IMPL_ID, &impl_id);
	if (!rc)
		rc = rpmi_base_get(r->puc, RPMI_BASE_GET_IMPL_VERSION,
				   &impl_version);
	if (!rc && r->rules && r->rules->setup)
		rc = r->rules->setup(r->puc, &r->state);
	if (rc)
		return rpmi_to_sbi(rc);

	/* Harts racing here have asked the same PuC the same questions. */
	r->ch.msg_prot_version = spec;
	r->group_version = version;
	r->impl_id = impl_id;
	r->impl_version = impl_version;
	r->probed = true;
	return SBI_SUCCESS;
}

static long mpxy_rpmi_read_attr(struct mpxy_channel *ch, uint32_t id,
				uint32_t *val)
{
	struct mpxy_rpmi *r = to_rpmi(ch);
	long rc = 0;

	if (id > MPXY_RPMI_ATTR_IMPL_VERSION)
		return SBI_ERR_BAD_RANGE;
	if (id == MPXY_RPMI_ATTR_SERVICEGROUP_ID) {
		*val = r->group;
		return SBI_SUCCESS;
	}

	rc = mpxy_rpmi_ask_puc(r);
	if (rc)
		return rc;
	*val = id == MPXY_RPMI_ATTR_SERVICEGROUP_VERSION ? r->group_version :
	       id == MPXY_RPMI_ATTR_IMPL_ID		 ? r->impl_id :
							   r->impl_version;
	return SBI_SUCCESS;
}

static long mpxy_rpmi_send(struct mpxy_channel *ch, uint32_t msg_id, void *buf,
			   unsigned long len, unsigned long resp_max,
			   unsigned long *resp_len)
{
	struct mpxy_rpmi *r = to_rpmi(ch);
	size_t got = 0;
	long rc = 0;

	/* An RPMI service id is 8 bits wide, and 0 is the notification. */
	if (msg_id == RPMI_SERVICE_NOTIFICATION || msg_id > 0xff)
		return SBI_ERR_NOT_SUPPORTED;
	if (!IS_ALIGNED(len, 4))
		return SBI_ERR_INVALID_PARAM;
	if (r->rules) {
		const struct rpmi_service_rule *rule =
			rpmi_service_rule_find(r->rules, (uint8_t)msg_id);

		if (!rule)
			return SBI_ERR_NOT_SUPPORTED;
		if (len < rule->req_min ||
		    (rule->req_max != RPMI_RULE_ANY_LEN && len > rule->req_max))
			return SBI_ERR_INVALID_PARAM;
	}
	rc = mpxy_rpmi_ask_puc(r);
	if (rc)
		return rc;

	/* A request the monitor answers itself, the way the PuC would have. */
	rc = r->rules && r->rules->filter ?
		     r->rules->filter(&r->state, (uint8_t)msg_id, buf) :
		     0;
	if (rc) {
		if (!resp_len || resp_max < 4)
			return rpmi_to_sbi((int)rc);
		*(uint32_t *)buf = (uint32_t)rc;
		*resp_len = 4;
		return SBI_SUCCESS;
	}

	if (!resp_len)
		return rpmi_to_sbi(rpmi_post(r->puc, r->group, (uint8_t)msg_id,
					     buf, len));

	rc = rpmi_to_sbi(rpmi_request(r->puc, r->group, (uint8_t)msg_id, buf,
				      len, buf, resp_max, &got));
	*resp_len = got;
	return rc;
}

/* rpmi_poll() found a notification of our group. */
static void mpxy_rpmi_event_sink(void *ctx, const void *events, size_t len)
{
	struct mpxy_rpmi *r = ctx;
	const uint32_t *ev = events;

	spin_lock(&r->lock);
	for (size_t off = 0; off < len;) {
		size_t sz =
			RPMI_EVENT_HDR_SIZE + RPMI_EVENT_DATALEN(ev[off / 4]);

		if (r->used + sz <= sizeof(r->events)) {
			memcpy((char *)r->events + r->used,
			       (const char *)ev + off, sz);
			r->used += sz;
			r->count++;
		} else {
			r->lost++;
		}
		off += sz;
	}
	spin_unlock(&r->lock);
}

static long mpxy_rpmi_get_events(struct mpxy_channel *ch, void *buf,
				 unsigned long max, struct mpxy_events *out)
{
	struct mpxy_rpmi *r = to_rpmi(ch);
	size_t bytes = 0;
	uint32_t n = 0;

	rpmi_poll(r->puc);

	spin_lock(&r->lock);
	while (n < r->count) {
		size_t sz = RPMI_EVENT_HDR_SIZE +
			    RPMI_EVENT_DATALEN(r->events[bytes / 4]);

		if (bytes + sz > max)
			break;
		bytes += sz;
		n++;
	}
	memcpy(buf, r->events, bytes);
	memmove(r->events, (char *)r->events + bytes, r->used - bytes);
	r->used -= bytes;
	r->count -= n;

	*out = (struct mpxy_events){
		.returned = n,
		.remaining = r->count,
		.lost = r->lost,
		.bytes = bytes,
	};
	r->lost = 0;
	spin_unlock(&r->lock);
	return SBI_SUCCESS;
}

static const struct mpxy_channel_ops mpxy_rpmi_ops = {
	.read_attr = mpxy_rpmi_read_attr,
	.send = mpxy_rpmi_send,
	.get_events = mpxy_rpmi_get_events,
};

static long mpxy_rpmi_channel_add(struct rpmi_context *puc, uint32_t channel_id,
				  uint16_t group)
{
	struct mpxy_rpmi *r = NULL;
	long rc = 0;

	/* BASE and CPPC are never proxied; these are M-mode only groups. */
	if (group == RPMI_GROUP_BASE || group == RPMI_GROUP_CPPC ||
	    group == RPMI_GROUP_SYSTEM_RESET ||
	    group == RPMI_GROUP_SYSTEM_SUSPEND || group == RPMI_GROUP_HSM)
		return SBI_ERR_DENIED;
	if (pool_used == CONFIG_MPXY_RPMI_MAX_CHANNELS)
		return SBI_ERR_FAILED;

	r = &pool[pool_used];
	r->puc = puc;
	r->group = group;
	r->rules = rpmi_group_rules_find(group);
	r->ch = (struct mpxy_channel){
		.id = channel_id,
		.msg_prot_id = MPXY_MSG_PROT_RPMI,
		.msg_prot_version = RPMI_VERSION(1, 0),
		.msg_data_max_len = rpmi_max_data_len(puc),
		.send_timeout_us = CONFIG_RPMI_TIMEOUT_US,
		/* Send, then wait for the acknowledgment. */
		.completion_timeout_us = 2 * CONFIG_RPMI_TIMEOUT_US,
		.capability =
			MPXY_CAP_SEND_WITH_RESP | MPXY_CAP_SEND_WITHOUT_RESP |
			MPXY_CAP_GET_NOTIFICATIONS | MPXY_CAP_EVENTS_STATE,
		.ops = &mpxy_rpmi_ops,
	};

	rc = mpxy_channel_register(&r->ch);
	if (rc)
		return rc;
	if (rpmi_event_sink_register(puc, group, mpxy_rpmi_event_sink, r))
		r->ch.capability &= ~(uint32_t)(MPXY_CAP_GET_NOTIFICATIONS |
						MPXY_CAP_EVENTS_STATE);
	pool_used++;
	return SBI_SUCCESS;
}

/*
 * A channel node: "mboxes = <&transport service-group>" says where the
 * messages go, "riscv,sbi-mpxy-channel-id" what S-mode calls the channel.
 * The compatible names the service group for whoever reads the tree; the
 * group that counts is the one in "mboxes".
 */
static int mpxy_rpmi_probe(const void *fdt, int node)
{
	struct rpmi_context *puc = NULL;
	uint16_t group = 0;
	int len = 0;
	const fdt32_t *id = NULL;

	if (node < 0)
		return 0;
	id = fdt_getprop(fdt, node, "riscv,sbi-mpxy-channel-id", &len);
	if (!id || len < 4 || rpmi_client_from_fdt(fdt, node, &puc, &group))
		return -1;
	return (int)mpxy_rpmi_channel_add(puc, fdt32_to_cpu(*id), group);
}

static const char *const mpxy_rpmi_compatible[] = {
	"riscv,rpmi-mpxy-clock",
	"riscv,rpmi-mpxy-device-power",
	"riscv,rpmi-mpxy-performance",
	"riscv,rpmi-mpxy-system-msi",
	"riscv,rpmi-mpxy-voltage",
	"riscv,rpmi-mpxy-mm",
	"riscv,rpmi-mpxy-logging",
	/* No binding names these yet: after the pattern of the others. */
	"riscv,rpmi-mpxy-ras-agent",
	"riscv,rpmi-mpxy-request-forward",
	/*
	 * An experimental or implementation specific group: "mboxes" says
	 * which.
	 */
	"riscv-tf,rpmi-mpxy-group",
	NULL,
};

DRIVER_DEFINE(mpxy_rpmi) = {
	.name = "mpxy-rpmi",
	.compatible = mpxy_rpmi_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = mpxy_rpmi_probe,
};
