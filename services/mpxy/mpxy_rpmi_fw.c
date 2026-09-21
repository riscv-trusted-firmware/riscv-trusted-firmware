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
 *   "riscv,rpmi-mpxy-reqfwd-bridge": a REQUEST_FORWARD channel like the
 *   first, of the domain "riscv,domain" names, with the channels it takes
 *   requests from as child nodes, each with its own "riscv,domain" and
 *   "riscv,sbi-mpxy-channel-id":
 *   "riscv,rpmi-mpxy-reqfwd-mm" is a MANAGEMENT_MODE channel as above,
 *   hosted by the bridge's domain.
 *
 * The bindings are the ones proposed for the same, under "riscv," names.
 *
 * How a request travels. Domains with harts of their own have the queue of
 * <reqfwd.h>: the producer waits in M-mode while a hart of the target
 * domain retrieves and completes the message. Through a bridge, domains
 * that share a hart need no second hart: the hart itself goes over
 * (<domain.h>, domain_switch_to()) with the message in a slot the monitor
 * keeps per domain and hart, the target finds it as its current message,
 * and completing it brings the hart back with the response. A bridge's
 * target that asks for a message when there is none gives the hart back
 * as well, to whoever entered it, or for a source domain to boot on it;
 * its RETRIEVE is run again when the hart returns, and then has a message.
 * Only where the hart has nowhere to go is the answer RPMI_ERR_NO_DATA. A
 * request for a bridge's domain from a hart that cannot go there (the
 * domain is not one the hart may run, or it runs there already some calls
 * down) takes the queue.
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <atomic.h>
#include <domain.h>
#include <driver.h>
#include <fdt_util.h>
#include <heap.h>
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
	/* ... of a bridge: the channels it takes requests from */
	bool bridge;
	struct fw_channel *sources, *next_source;
	/* MANAGEMENT_MODE */
	struct fw_channel *bridge_target; /* NULL: by the queue alone */
	unsigned int target;
	uint64_t shmem_base, shmem_size;
	unsigned long token; /* atomic */
};

static struct fw_channel pool[CONFIG_MPXY_RPMI_FW_MAX_CHANNELS];
static unsigned int pool_used;

/*
 * A message that travels with the hart, per source domain and hart. The
 * largest a source forwards is MM_COMMUNICATE, and its answer two words.
 */
enum { SLOT_FREE, SLOT_SENT, SLOT_RETRIEVED, SLOT_COMPLETED, SLOT_FAILED };

struct bridge_slot {
	unsigned int state;
	int error; /* SLOT_FAILED: why */
	const struct fw_channel *to;
	uint32_t out_size; /* MM_COMMUNICATE: the output area's */
	size_t msg_len, rsp_len;
	uint32_t msg[2 + 4], rsp[2];
};

/* What a channel operation leaves for mpxy_switch_pending(), per hart. */
enum { SWITCH_NONE, SWITCH_TO, SWITCH_BACK, SWITCH_YIELD };

struct switch_request {
	unsigned int kind;
	struct fw_channel *ch; /* TO: the source, YIELD: the bridge */
};

/* There with the first bridge. */
static struct bridge_slot *slots;
static struct switch_request *requests;

static struct bridge_slot *slot_of(unsigned int key)
{
	return domain_hart_slot(slots, sizeof(*slots), key, this_hart_index());
}

static void switch_request(unsigned int kind, struct fw_channel *ch)
{
	requests[this_hart_index()] =
		(struct switch_request){ .kind = kind, .ch = ch };
}

/* The message the hart came to channel 'c' with, NULL if it did not. */
static struct bridge_slot *slot_for(const struct fw_channel *c)
{
	int key = domain_caller_key();
	struct bridge_slot *s = NULL;

	if (!slots || key < 0)
		return NULL;
	s = slot_of((unsigned int)key);
	return s->to == c && (s->state == SLOT_SENT ||
			      s->state == SLOT_RETRIEVED) ?
		       s :
		       NULL;
}

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
	struct bridge_slot *s = slot_for(c);
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
		if (s && buf[0] >= s->msg_len) {
			rc = RPMI_ERR_INVALID_PARAM;
		} else if (s) {
			returned = MIN(s->msg_len - buf[0], max);
			remaining = s->msg_len - buf[0] - returned;
			memcpy(&buf[3], (const char *)s->msg + buf[0],
			       returned);
			s->state = SLOT_RETRIEVED;
			rc = RPMI_SUCCESS;
		} else {
			rc = reqfwd_retrieve(c->queue, buf[0], &buf[3], max,
					     &returned, &remaining);
		}
		if (rc == RPMI_ERR_NO_DATA && c->bridge) {
			/*
			 * Somebody else's turn on this hart, if there is
			 * somebody; the request stays as it is, to be made
			 * again.
			 */
			switch_request(SWITCH_YIELD, c);
			*resp_len = 4;
			return SBI_SUCCESS;
		}
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
		if (s && s->state == SLOT_RETRIEVED) {
			bool fits = len <= sizeof(s->rsp);

			if (fits)
				memcpy(s->rsp, buf, len);
			s->rsp_len = len;
			s->state = fits ? SLOT_COMPLETED : SLOT_FAILED;
			s->error = RPMI_ERR_BAD_RANGE;
			rc = fits ? RPMI_SUCCESS : RPMI_ERR_BAD_RANGE;
			left = reqfwd_count(c->queue);
			/*
			 * The hart takes the answer back, and this call returns
			 * later.
			 */
			switch_request(SWITCH_BACK, c);
		} else {
			rc = reqfwd_complete(c->queue, buf, len, &left);
		}
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

	/* With this very hart, where that gets the request there... */
	if (c->bridge_target && domain_enterable(domain_by_index(c->target))) {
		struct bridge_slot *s = slot_of(this_domain_key());

		*s = (struct bridge_slot){
			.state = SLOT_SENT,
			.to = c->bridge_target,
			.out_size = out_size,
			.msg_len = sizeof(fwd),
		};
		memcpy(s->msg, &fwd, sizeof(fwd));
		/* The answer is mpxy_context_resumed()'s to give. */
		switch_request(SWITCH_TO, c);
		*resp_len = 8;
		return SBI_SUCCESS;
	}
	/*
	 * ... or else the domain that hosts it has to be there to take
	 * requests.
	 */
	queue = reqfwd_queue_of(c->target);
	if (!queue) {
		rc = RPMI_ERR_NOT_SUPPORTED;
		goto out;
	}

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
 * ---- messages that travel with the hart -----------------------------------
 */

/* The answer to the MM_COMMUNICATE the context sent before the hart left it. */
void mpxy_context_resumed(struct trap_regs *regs)
{
	struct bridge_slot *s = NULL;
	uint32_t *mem = NULL, status = 0, written = 0;

	if (!slots)
		return;
	s = slot_of(this_domain_key());
	if (s->state == SLOT_FREE)
		return;

	if (s->state == SLOT_FAILED)
		status = (uint32_t)s->error;
	else if (s->state != SLOT_COMPLETED || s->rsp_len != sizeof(s->rsp) ||
		 s->rsp[1] > s->out_size)
		/*
		 * The hart is back without an answer, or with one not to pass
		 * on.
		 */
		status = (uint32_t)RPMI_ERR_IO;
	else
		status = s->rsp[0], written = s->rsp[1];
	s->state = SLOT_FREE;

	mpxy_shmem_access(true);
	mem = mpxy_hart_shmem();
	if (mem) {
		mem[0] = status;
		mem[1] = written;
	}
	mpxy_shmem_access(false);
	regs->a0 = SBI_SUCCESS;
	regs->a1 = 8;
}

bool mpxy_switch_pending(struct trap_regs *regs, long error, long value)
{
	struct switch_request *r = NULL;
	struct bridge_slot *s = NULL;
	unsigned int kind = 0;
	uint32_t *mem = NULL;

	if (!requests)
		return false;
	r = &requests[this_hart_index()];
	kind = r->kind;
	r->kind = SWITCH_NONE;

	switch (kind) {
	case SWITCH_TO:
		if (domain_switch_to(regs, domain_by_index(r->ch->target))) {
			/*
			 * It could a moment ago: its domain is being stopped.
			 */
			s = slot_of(this_domain_key());
			s->state = SLOT_FAILED;
			s->error = RPMI_ERR_BUSY;
			mpxy_context_resumed(regs);
		}
		return true;
	case SWITCH_BACK:
		/* What COMPLETE returns, whenever this context runs again. */
		regs->a0 = (unsigned long)error;
		regs->a1 = (unsigned long)value;
		domain_switch_back(regs, NULL);
		return true;
	case SWITCH_YIELD:
		/*
		 * The ecall once more when the hart is back: by then there is a
		 * message.
		 */
		regs->mepc -= 4;
		if (!domain_switch_back(regs, NULL))
			return true;
		for (struct fw_channel *src = r->ch->sources; src;
		     src = src->next_source) {
			struct domain *owner =
				domain_by_index(src->ch.owner - 1);

			if (!domain_switch_back(regs, owner))
				return true;
		}
		/* Nowhere to go: the queue is empty, and that is the answer. */
		regs->mepc += 4;
		mpxy_shmem_access(true);
		mem = mpxy_hart_shmem();
		if (mem)
			mem[0] = (uint32_t)RPMI_ERR_NO_DATA;
		mpxy_shmem_access(false);
		return false;
	default:
		return false;
	}
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

/*
 * Management mode of the node's domain, hosted by 'target' (behind 'bridge', if
 * any).
 */
static int mm_channel_add(const void *fdt, int node,
			  const struct domain *target,
			  struct fw_channel *bridge)
{
	struct fw_channel *c = NULL;
	const fdt32_t *base = NULL;
	uint32_t order = 0, phandle = 0;
	int region = 0, len = 0;

	c = fw_channel_new(fdt, node, RPMI_GROUP_MANAGEMENT_MODE);
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
	if (bridge) {
		c->bridge_target = bridge;
		c->next_source = bridge->sources;
		bridge->sources = c;
	}
	pr_info("mpxy: channel %x, management mode of domain %s hosted by %s%s\n",
		c->ch.id, c->owner->name, target->name,
		bridge ? ", bridged" : "");
	return 0;
}

static int mm_probe(const void *fdt, int node)
{
	uint32_t target = 0;

	if (node < 0)
		return 0;
	target = fdt_prop_u32(fdt, node, "riscv,reqfwd-target", 0);
	return mm_channel_add(fdt, node, domain_by_phandle(target), NULL);
}

static const char *const bridge_mm_compatible[] = { "riscv,rpmi-mpxy-reqfwd-mm",
						    NULL };

static int bridge_probe(const void *fdt, int node)
{
	struct fw_channel *c = NULL;
	int child = 0;

	if (node < 0)
		return 0;
	c = fw_channel_new(fdt, node, RPMI_GROUP_REQUEST_FORWARD);
	if (!c)
		return -1;
	c->bridge = true;
	c->wakeup_ssip = fdt_getprop(fdt, node, "riscv,wakeup-ssip", NULL);
	c->ch.capability |= MPXY_CAP_GET_NOTIFICATIONS;
	c->queue = reqfwd_serve(c->owner->index, reqfwd_notify, c);
	if (!c->queue || mpxy_channel_register(&c->ch))
		return -1;
	pool_used++;
	if (!slots) {
		slots = domain_hart_alloc(sizeof(*slots));
		requests =
			heap_alloc_array(hart_table_size(), sizeof(*requests));
	}
	pr_info("mpxy: channel %x, bridge to domain %s\n", c->ch.id,
		c->owner->name);

	fdt_for_each_subnode(child, fdt, node) {
		const char *name = fdt_get_name(fdt, child, NULL);

		/*
		 * What is forwarded is a service group's messages: the ones
		 * known here.
		 */
		if (!fdt_node_compatible_any(fdt, child, bridge_mm_compatible))
			pr_warn("mpxy: bridge source %s: not a service group served here\n",
				name);
		else if (mm_channel_add(fdt, child, c->owner, c))
			pr_warn("mpxy: bridge source %s: ignored (bad node)\n",
				name);
	}
	return 0;
}

static const char *const reqfwd_compatible[] = {
	"riscv,rpmi-mpxy-request-forward", NULL
};

static const char *const mm_compatible[] = { "riscv,rpmi-mpxy-mm-domain",
					     NULL };
static const char *const bridge_compatible[] = {
	"riscv,rpmi-mpxy-reqfwd-bridge", NULL
};

DRIVER_DEFINE(mpxy_rpmi_reqfwd) = {
	.name = "mpxy-rpmi-request-forward",
	.compatible = reqfwd_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = reqfwd_probe,
};

DRIVER_DEFINE(mpxy_rpmi_reqfwd_bridge) = {
	.name = "mpxy-rpmi-reqfwd-bridge",
	.compatible = bridge_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = bridge_probe,
};

/*
 * A bridge's source is its bridge's to probe; the monitor's alone all the same.
 */
static int bridge_source_probe(const void *fdt, int node)
{
	return 0;
}

DRIVER_DEFINE(mpxy_rpmi_reqfwd_source) = {
	.name = "mpxy-rpmi-reqfwd-source",
	.compatible = bridge_mm_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = bridge_source_probe,
};

DRIVER_DEFINE(mpxy_rpmi_mm_domain) = {
	.name = "mpxy-rpmi-mm-domain",
	.compatible = mm_compatible,
	.stage = DRIVER_STAGE_LATE,
	.mmode_only = true,
	.probe = mm_probe,
};
