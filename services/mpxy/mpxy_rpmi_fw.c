// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI service groups the monitor serves itself, as MPXY channels.
 *
 * RPMI over MPXY as with a platform microcontroller behind the channel
 * (mpxy_rpmi.c): message_id is the RPMI service, the message data the
 * request data, the response the acknowledgment data with its STATUS
 * word. What is served is what RPMI gives the SBI implementation to do,
 * forwarding requests from one domain (<domain.h>) to another:
 *
 *   "riscv,rpmi-mpxy-request-forward" without "mboxes": REQUEST_FORWARD.
 *   The domain that owns the channel ("riscv,domain") takes the
 *   requests forwarded to it, a piece at a time, and completes them with a
 *   response. REQFWD_NEW_MESSAGE tells it of a message in an empty queue,
 *   signalled like any notification event (MSI, SSE); with
 *   "riscv,wakeup-ssip" the domain's harts get a supervisor software
 *   interrupt too, for software that just waits for one.
 *
 *   "riscv,rpmi-mpxy-mm-domain": MANAGEMENT_MODE, hosted by another
 *   domain ("riscv,reqfwd-target"). MM_COMMUNICATE is forwarded there as
 *   the RPMI message it would have been for a PuC; the caller waits for
 *   the answer, "riscv,sbi-mpxy-completion-timeout-us" at most. The MM
 *   shared memory is a domain memory region ("riscv,mm-memregion") both
 *   domains have; the monitor checks offsets against it and never touches
 *   it.
 *
 * The bindings are the ones proposed for the same, under "riscv," names.
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <atomic.h>
#include <domain.h>
#include <driver.h>
#include <fdt_util.h>
#include <ipi.h>
#include <libfdt.h>
#include <log.h>
#include <mpxy.h>
#include <reqfwd.h>
#include <sbi/sbi.h>
#include <string.h>
#include <util.h>

#include "mpxy_rpmi.h"

#define MM_VERSION RPMI_VERSION(1, 0)
#define GROUP_VERSION RPMI_VERSION(1, 0)
#define DEFAULT_TIMEOUT_US 1000000
/* Of a forwarded message, in REQFWD_NEW_MESSAGE: its header and a bit. */
#define NEW_MESSAGE_BYTES 16

struct fw_channel {
	struct mpxy_channel ch;
	uint16_t group;
	/* REQUEST_FORWARD */
	struct reqfwd_queue *queue;
	const struct domain *owner;
	bool wakeup_ssip;
	unsigned long notify_enabled;
	unsigned long new_message; /* atomic: announced, not fetched yet */
	/* MANAGEMENT_MODE */
	unsigned int target;
	uint64_t shmem_base, shmem_size;
	unsigned long token; /* atomic */
};

static struct fw_channel pool[CONFIG_MPXY_RPMI_FW_MAX_CHANNELS];
static unsigned int pool_used;

/* The channel is the first member, and as well aligned as what is around it. */
static struct fw_channel *to_fw(struct mpxy_channel *ch)
{
	return (struct fw_channel *)(void *)ch;
}

static long fw_read_attr(struct mpxy_channel *ch, uint32_t id, uint32_t *val)
{
	switch (id) {
	case MPXY_RPMI_ATTR_SERVICEGROUP_ID:
		*val = to_fw(ch)->group;
		return SBI_SUCCESS;
	case MPXY_RPMI_ATTR_SERVICEGROUP_VERSION:
		*val = GROUP_VERSION;
		return SBI_SUCCESS;
	/* Who implements the group: this SBI implementation. */
	case MPXY_RPMI_ATTR_IMPL_ID:
		*val = CONFIG_SBI_IMPL_ID;
		return SBI_SUCCESS;
	case MPXY_RPMI_ATTR_IMPL_VERSION:
		*val = CONFIG_SBI_IMPL_VERSION;
		return SBI_SUCCESS;
	default:
		return SBI_ERR_BAD_RANGE;
	}
}

/* The answer is a status and nothing else. */
static long status_only(uint32_t *buf, unsigned long *resp_len, int status)
{
	buf[0] = (uint32_t)status;
	*resp_len = 4;
	return SBI_SUCCESS;
}

/*
 * ENABLE_NOTIFICATION of both groups: (EVENT_ID, REQ_STATE) -> (STATUS, STATE).
 */
static long enable_notification(struct fw_channel *c, uint32_t *buf,
				unsigned long len, unsigned long *resp_len,
				uint32_t event)
{
	if (len != 8 || buf[1] > 2)
		return status_only(buf, resp_len, RPMI_ERR_INVALID_PARAM);
	if (buf[0] != event || !event)
		return status_only(buf, resp_len, RPMI_ERR_NOT_SUPPORTED);
	if (buf[1] < 2)
		c->notify_enabled = buf[1];
	buf[0] = RPMI_SUCCESS;
	buf[1] = (uint32_t)c->notify_enabled;
	*resp_len = 8;
	return SBI_SUCCESS;
}

/*
 * ---- REQUEST_FORWARD ------------------------------------------------------
 */

/* A message in an empty queue, on the producer's hart: the owner is told. */
static void reqfwd_notify(void *arg)
{
	struct fw_channel *c = arg;
	struct hartmask harts = {};

	if (c->notify_enabled) {
		atomic_store_ulong(&c->new_message, 1);
		mpxy_channel_events_due(&c->ch);
	}
	if (c->wakeup_ssip) {
		hsm_interruptible_mask_of(c->owner, &harts);
		ipi_send_mask(&harts, IPI_EVENT_SMODE);
	}
}

static long reqfwd_service(struct fw_channel *c, uint32_t service,
			   uint32_t *buf, unsigned long len,
			   unsigned long resp_max, unsigned long *resp_len)
{
	size_t returned = 0, remaining = 0, left = 0, max = 0;
	int rc = 0;

	switch (service) {
	case RPMI_SERVICE_ENABLE_NOTIFICATION:
		return enable_notification(c, buf, len, resp_len,
					   RPMI_REQFWD_EVENT_NEW_MESSAGE);
	case RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE:
		if (len != 4)
			return status_only(buf, resp_len,
					   RPMI_ERR_INVALID_PARAM);
		/*
		 * What fits the channel after (STATUS, REMAINING, RETURNED), in
		 * words.
		 */
		max = MIN((unsigned long)c->ch.msg_data_max_len, resp_max);
		max = ROUNDDOWN2(max - 12, 4);
		rc = reqfwd_retrieve(c->queue, buf[0], &buf[3], max, &returned,
				     &remaining);
		if (rc)
			return status_only(buf, resp_len, rc);
		buf[0] = RPMI_SUCCESS;
		buf[1] = (uint32_t)remaining;
		buf[2] = (uint32_t)returned;
		*resp_len = 12 + ROUNDUP2(returned, 4);
		return SBI_SUCCESS;
	case RPMI_REQFWD_COMPLETE_CURRENT_MESSAGE:
		/*
		 * The response data is all of the message; it starts with a
		 * STATUS.
		 */
		if (len < 4 || !IS_ALIGNED(len, 4))
			return status_only(buf, resp_len,
					   RPMI_ERR_INVALID_PARAM);
		rc = reqfwd_complete(c->queue, buf, len, &left);
		buf[0] = (uint32_t)rc;
		buf[1] = (uint32_t)left;
		*resp_len = 8;
		return SBI_SUCCESS;
	default:
		return SBI_ERR_NOT_SUPPORTED;
	}
}

/* One event at most: the message that is current now, if it is still news. */
static long fw_get_events(struct mpxy_channel *ch, void *buf, unsigned long max,
			  struct mpxy_events *ev)
{
	struct fw_channel *c = to_fw(ch);
	uint32_t *out = buf;
	size_t n = 0;

	if (!c->queue || max < RPMI_EVENT_HDR_SIZE + NEW_MESSAGE_BYTES ||
	    !atomic_swap_ulong(&c->new_message, 0))
		return SBI_SUCCESS;
	n = ROUNDDOWN2(reqfwd_peek(c->queue, &out[1], NEW_MESSAGE_BYTES), 4);
	if (!n)
		return SBI_SUCCESS;
	out[0] = (RPMI_REQFWD_EVENT_NEW_MESSAGE << 16) | (uint32_t)n;
	ev->returned = 1;
	ev->bytes = RPMI_EVENT_HDR_SIZE + n;
	return SBI_SUCCESS;
}

/*
 * ---- MANAGEMENT_MODE, hosted by a domain ----------------------------------
 */

static bool mm_range_ok(const struct fw_channel *c, uint32_t off, uint32_t size)
{
	return off <= c->shmem_size && size <= c->shmem_size - off;
}

static long mm_service(struct fw_channel *c, uint32_t service, uint32_t *buf,
		       unsigned long len, unsigned long *resp_len)
{
	/* In the monitor's memory, not the caller's: the queue's rule. */
	struct {
		struct rpmi_hdr hdr;
		uint32_t data[4];
	} fwd;
	uint32_t rsp[2] = { 0 }, out_size = 0;
	struct reqfwd_queue *queue = NULL;
	size_t rsp_len = 0;
	int rc = 0;

	switch (service) {
	case RPMI_SERVICE_ENABLE_NOTIFICATION:
		/* The group has no events. */
		return enable_notification(c, buf, len, resp_len, 0);
	case RPMI_MM_GET_ATTRIBUTES:
		if (len)
			return status_only(buf, resp_len,
					   RPMI_ERR_INVALID_PARAM);
		buf[0] = RPMI_SUCCESS;
		buf[1] = MM_VERSION;
		buf[2] = (uint32_t)c->shmem_base;
		buf[3] = high32_from_64(c->shmem_base);
		buf[4] = (uint32_t)c->shmem_size;
		*resp_len = 20;
		return SBI_SUCCESS;
	case RPMI_MM_COMMUNICATE:
		break;
	default:
		return SBI_ERR_NOT_SUPPORTED;
	}

	if (len != 16)
		return status_only(buf, resp_len, RPMI_ERR_INVALID_PARAM);
	if (!mm_range_ok(c, buf[0], buf[1]) ||
	    !mm_range_ok(c, buf[2], buf[3])) {
		rc = RPMI_ERR_INVALID_ADDR;
		goto out;
	}
	/* The domain that hosts it has to be there to take requests. */
	queue = reqfwd_queue_of(c->target);
	if (!queue) {
		rc = RPMI_ERR_NOT_SUPPORTED;
		goto out;
	}

	/* The message a PuC would have been sent. */
	fwd.hdr = (struct rpmi_hdr){
		.group = RPMI_GROUP_MANAGEMENT_MODE,
		.service = RPMI_MM_COMMUNICATE,
		.flags = RPMI_MSG_NORMAL_REQUEST,
		.datalen = sizeof(fwd.data),
		.token = (uint16_t)atomic_add_ulong(&c->token, 1),
	};
	memcpy(fwd.data, buf, sizeof(fwd.data));
	out_size = buf[3];

	rc = reqfwd_send(queue, &fwd, sizeof(fwd), rsp, sizeof(rsp), &rsp_len,
			 c->ch.completion_timeout_us);
	/*
	 * The caller reads as many bytes of the output area as it is told
	 * were written: more than the area has is not an answer to pass on.
	 */
	if (!rc && (rsp_len != sizeof(rsp) || rsp[1] > out_size))
		rc = RPMI_ERR_IO;
	if (!rc) {
		buf[0] = rsp[0];
		buf[1] = rsp[1];
		*resp_len = 8;
		return SBI_SUCCESS;
	}
out:
	buf[0] = (uint32_t)rc;
	buf[1] = 0;
	*resp_len = 8;
	return SBI_SUCCESS;
}

/*
 * ---- the channels ---------------------------------------------------------
 */

static long fw_send(struct mpxy_channel *ch, uint32_t msg_id, void *buf,
		    unsigned long len, unsigned long resp_max,
		    unsigned long *resp_len)
{
	struct fw_channel *c = to_fw(ch);
	unsigned long got = 0;
	long rc = 0;

	/*
	 * RPMI data comes in words; a service id is a byte; the groups have no
	 * posted requests.
	 */
	if (!IS_ALIGNED(len, 4) || msg_id > 0xff)
		return SBI_ERR_INVALID_PARAM;
	if (!resp_len || resp_max < 20)
		return SBI_ERR_NOT_SUPPORTED;
	rc = c->group == RPMI_GROUP_REQUEST_FORWARD ?
		     reqfwd_service(c, msg_id, buf, len, resp_max, &got) :
		     mm_service(c, msg_id, buf, len, &got);
	*resp_len = got;
	return rc;
}

static const struct mpxy_channel_ops fw_ops = {
	.read_attr = fw_read_attr,
	.send = fw_send,
	.get_events = fw_get_events,
};

static struct fw_channel *fw_channel_new(const void *fdt, int node,
					 uint16_t group)
{
	struct fw_channel *c = &pool[pool_used];
	const struct domain *owner = NULL;
	unsigned int owner_id = 0;
	int len = 0;
	const fdt32_t *id =
		fdt_getprop(fdt, node, "riscv,sbi-mpxy-channel-id", &len);

	/* Forwarding is between domains: such a channel is some domain's. */
	if (pool_used == CONFIG_MPXY_RPMI_FW_MAX_CHANNELS || !id || len < 4 ||
	    mpxy_channel_owner_from_fdt(fdt, node, &owner_id) ||
	    owner_id == MPXY_OWNER_ANY)
		return NULL;
	owner = domain_by_index(owner_id - 1);

	*c = (struct fw_channel){ .group = group, .owner = owner };
	c->ch = (struct mpxy_channel){
		.id = fdt32_to_cpu(*id),
		.msg_prot_id = MPXY_MSG_PROT_RPMI,
		.msg_prot_version = RPMI_VERSION(1, 0),
		.msg_data_max_len = fdt_prop_u32(fdt, node,
						 "riscv,sbi-mpxy-msg-max-len",
						 256),
		.completion_timeout_us =
			fdt_prop_u32(fdt, node,
				     "riscv,sbi-mpxy-completion-timeout-us",
				     DEFAULT_TIMEOUT_US),
		.capability = MPXY_CAP_SEND_WITH_RESP,
		.owner = owner_id,
		.ops = &fw_ops,
	};
	/*
	 * Room for the longest answer there is, and for a piece of a message.
	 */
	if (c->ch.msg_data_max_len < 20 ||
	    c->ch.msg_data_max_len > MPXY_SHMEM_SIZE ||
	    !IS_ALIGNED(c->ch.msg_data_max_len, 4))
		return NULL;
	return c;
}

static int reqfwd_probe(const void *fdt, int node)
{
	struct fw_channel *c = NULL;

	/*
	 * With "mboxes" it is a PuC's REQUEST_FORWARD, and mpxy_rpmi.c's node.
	 */
	if (node < 0 || fdt_getprop(fdt, node, "mboxes", NULL))
		return 0;
	c = fw_channel_new(fdt, node, RPMI_GROUP_REQUEST_FORWARD);
	if (!c)
		return -1;
	c->wakeup_ssip = fdt_getprop(fdt, node, "riscv,wakeup-ssip", NULL);
	c->ch.capability |= MPXY_CAP_GET_NOTIFICATIONS;
	c->queue = reqfwd_serve(c->owner->index, reqfwd_notify, c);
	if (!c->queue || mpxy_channel_register(&c->ch))
		return -1;
	pool_used++;
	pr_info("mpxy: channel %x, requests forwarded to domain %s\n", c->ch.id,
		c->owner->name);
	return 0;
}

static int mm_probe(const void *fdt, int node)
{
	const struct domain *target = NULL;
	struct fw_channel *c = NULL;
	const fdt32_t *base = NULL;
	uint32_t order = 0, phandle = 0;
	int region = 0, len = 0;

	if (node < 0)
		return 0;
	c = fw_channel_new(fdt, node, RPMI_GROUP_MANAGEMENT_MODE);
	target = domain_by_phandle(fdt_prop_u32(fdt, node,
						"riscv,reqfwd-target", 0));
	phandle = fdt_prop_u32(fdt, node, "riscv,mm-memregion", 0);
	region = fdt_node_offset_by_phandle(fdt, phandle);
	if (!c || !target || target == c->owner || region < 0 ||
	    fdt_node_check_compatible(fdt, region, "riscv,domain,memregion"))
		return -1;
	base = fdt_getprop(fdt, region, "base", &len);
	order = fdt_prop_u32(fdt, region, "order", 0);
	/* MM_SHMEM_SIZE is 32 bits wide. */
	if (!base || len < 8 || order < 3 || order > 31)
		return -1;
	c->target = target->index;
	c->shmem_base =
		reg_pair_to_64(fdt32_to_cpu(base[0]), fdt32_to_cpu(base[1]));
	c->shmem_size = BIT64(order);
	if (mpxy_channel_register(&c->ch))
		return -1;
	pool_used++;
	pr_info("mpxy: channel %x, management mode of domain %s hosted by %s\n",
		c->ch.id, c->owner->name, target->name);
	return 0;
}

static const char *const reqfwd_compatible[] = {
	"riscv,rpmi-mpxy-request-forward", NULL
};

static const char *const mm_compatible[] = { "riscv,rpmi-mpxy-mm-domain",
					     NULL };

DRIVER_DEFINE(mpxy_rpmi_reqfwd) = {
	.name = "mpxy-rpmi-request-forward",
	.compatible = reqfwd_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = reqfwd_probe,
};

DRIVER_DEFINE(mpxy_rpmi_mm_domain) = {
	.name = "mpxy-rpmi-mm-domain",
	.compatible = mm_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = mm_probe,
};
