// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI client. Per context, one request is in flight at a time: the
 * requester holds the context's lock from the enqueue until its
 * acknowledgment shows up (or the timeout expires) and polls for it;
 * acknowledgments carrying another token are leftovers (an earlier
 * timeout, a previous firmware) and are dropped.
 */

#include <atomic.h>
#include <fdt_util.h>
#include <ipi.h>
#include <log.h>
#include <rpmi.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <util.h>

static struct rpmi_context *contexts;
static unsigned int nr_contexts;

void rpmi_context_register(struct rpmi_context *ctx)
{
	ctx->next = contexts;
	contexts = ctx;
	nr_contexts++;
}

struct rpmi_context *rpmi_context_find(uint32_t phandle)
{
	for (struct rpmi_context *ctx = contexts; ctx; ctx = ctx->next)
		if (ctx->phandle == phandle)
			return ctx;
	return NULL;
}

unsigned int rpmi_context_count(void)
{
	return nr_contexts;
}

uint32_t rpmi_max_data_len(const struct rpmi_context *ctx)
{
	return ctx->max_data_len < CONFIG_RPMI_MAX_DATA_LEN ?
		       ctx->max_data_len :
		       CONFIG_RPMI_MAX_DATA_LEN;
}

int rpmi_client_from_fdt(const void *fdt, int node, struct rpmi_context **ctx,
			 uint16_t *group)
{
	int len = 0;
	const fdt32_t *mboxes = fdt_getprop(fdt, node, "mboxes", &len);

	/* <phandle service-group>: the transports have #mbox-cells = <1>. */
	if (!mboxes || len < 8)
		return RPMI_ERR_INVALID_PARAM;
	*ctx = rpmi_context_find(fdt32_to_cpu(mboxes[0]));
	*group = (uint16_t)fdt32_to_cpu(mboxes[1]);
	return *ctx ? RPMI_SUCCESS : RPMI_ERR_NOT_SUPPORTED;
}

/* Waiting for another hart or the PuC: keep serving our own IPIs. */
static void context_lock(struct rpmi_context *ctx)
{
	while (atomic_swap_ulong(&ctx->lock, 1))
		ipi_process();
}

static void context_unlock(struct rpmi_context *ctx)
{
	atomic_store_ulong(&ctx->lock, 0);
}

static uint64_t deadline(void)
{
	return timer_now() + timer_usecs_to_ticks(CONFIG_RPMI_TIMEOUT_US);
}

static bool expired(uint64_t when)
{
	/* Without a timer there is nothing to measure a timeout with. */
	return timer_available() && timer_now() > when;
}

/* Enqueue, waiting for room; called with the lock held. */
static int send_locked(struct rpmi_context *ctx, enum rpmi_queue q,
		       const struct rpmi_hdr *hdr, const void *data)
{
	uint64_t until = deadline();
	int rc = 0;

	while ((rc = ctx->send(ctx, q, hdr, data)) == RPMI_ERR_BUSY) {
		if (expired(until))
			return RPMI_ERR_TIMEOUT;
		ipi_process();
		cpu_relax();
	}
	return rc;
}

static int request_check(const struct rpmi_context *ctx, size_t req_len)
{
	if (!ctx)
		return RPMI_ERR_NOT_SUPPORTED;
	if (!IS_ALIGNED(req_len, 4) || req_len > rpmi_max_data_len(ctx))
		return RPMI_ERR_INVALID_PARAM;
	return RPMI_SUCCESS;
}

/* Poll for the acknowledgment of 'req'; called with the lock held. */
static int wait_ack(struct rpmi_context *ctx, const struct rpmi_hdr *req,
		    void *resp, size_t resp_max, size_t *resp_len)
{
	uint64_t until = deadline();
	struct rpmi_hdr ack = {};

	for (;;) {
		int rc = ctx->recv(ctx, RPMI_QUEUE_P2A_ACK, &ack, resp,
				   resp_max);

		if (rc == RPMI_ERR_NO_DATA) {
			if (expired(until))
				return RPMI_ERR_TIMEOUT;
			ipi_process();
			cpu_relax();
			continue;
		}
		if (rc && rc != RPMI_ERR_BAD_RANGE)
			return rc;

		if (ack.token != req->token || ack.group != req->group ||
		    ack.service != req->service ||
		    (ack.flags & RPMI_FLAGS_TYPE_MASK) !=
			    RPMI_MSG_ACKNOWLEDGEMENT) {
			pr_dbg("rpmi: dropped stale acknowledgment, token %u\n",
			       ack.token);
			continue;
		}
		/* Ours: it must fit, and start with a STATUS word. */
		if (rc || ack.datalen < 4)
			return RPMI_ERR_IO;
		*resp_len = ack.datalen;
		return RPMI_SUCCESS;
	}
}

int rpmi_request(struct rpmi_context *ctx, uint16_t group, uint8_t service,
		 const void *req, size_t req_len, void *resp, size_t resp_max,
		 size_t *resp_len)
{
	struct rpmi_hdr hdr = {
		.group = group,
		.service = service,
		.flags = RPMI_MSG_NORMAL_REQUEST,
		.datalen = (uint16_t)req_len,
	};
	int rc = request_check(ctx, req_len);

	if (rc)
		return rc;

	context_lock(ctx);
	hdr.token = ctx->next_token++;
	rc = send_locked(ctx, RPMI_QUEUE_A2P_REQ, &hdr, req);
	if (!rc)
		rc = wait_ack(ctx, &hdr, resp, resp_max, resp_len);
	context_unlock(ctx);
	return rc;
}

int rpmi_post(struct rpmi_context *ctx, uint16_t group, uint8_t service,
	      const void *req, size_t req_len)
{
	struct rpmi_hdr hdr = {
		.group = group,
		.service = service,
		.flags = RPMI_MSG_POSTED_REQUEST,
		.datalen = (uint16_t)req_len,
	};
	int rc = request_check(ctx, req_len);

	if (rc)
		return rc;

	context_lock(ctx);
	hdr.token = ctx->next_token++;
	rc = send_locked(ctx, RPMI_QUEUE_A2P_REQ, &hdr, req);
	context_unlock(ctx);
	return rc;
}

int rpmi_call(struct rpmi_context *ctx, uint16_t group, uint8_t service,
	      const uint32_t *req, unsigned int req_words, uint32_t *resp,
	      unsigned int resp_words)
{
	size_t len = 0;
	int rc = 0;

	rc = rpmi_request(ctx, group, service, req, 4 * req_words, resp,
			  4 * resp_words, &len);
	if (rc)
		return rc;
	if ((int32_t)resp[0])
		return (int32_t)resp[0];
	return len < 4 * resp_words ? RPMI_ERR_IO : RPMI_SUCCESS;
}

/* A BASE service without request data that returns (STATUS, value). */
int rpmi_base_get(struct rpmi_context *ctx, uint8_t service, uint32_t *value)
{
	uint32_t resp[2] = { 0 };
	int rc = rpmi_call(ctx, RPMI_GROUP_BASE, service, NULL, 0, resp, 2);

	if (!rc)
		*value = resp[1];
	return rc;
}

int rpmi_probe_group(struct rpmi_context *ctx, uint16_t group,
		     uint32_t *version)
{
	uint32_t req = group, resp[2] = { 0 };
	int rc = rpmi_call(ctx, RPMI_GROUP_BASE, RPMI_BASE_PROBE_SERVICE_GROUP,
			   &req, 1, resp, 2);

	if (!rc)
		*version = resp[1];
	return rc;
}

int rpmi_event_sink_register(struct rpmi_context *ctx, uint16_t group,
			     rpmi_event_sink_t sink, void *sink_ctx)
{
	if (ctx->nr_sinks == RPMI_MAX_EVENT_SINKS)
		return RPMI_ERR_FAILED;
	ctx->sinks[ctx->nr_sinks].group = group;
	ctx->sinks[ctx->nr_sinks].fn = sink;
	ctx->sinks[ctx->nr_sinks].ctx = sink_ctx;
	ctx->nr_sinks++;
	return RPMI_SUCCESS;
}

/*
 * Hand over the well-formed events of a notification; stop at the first bad
 * one.
 */
static void deliver_events(struct rpmi_context *ctx, const struct rpmi_hdr *hdr)
{
	size_t len = 0;

	while (len + RPMI_EVENT_HDR_SIZE <= hdr->datalen) {
		size_t next = len + RPMI_EVENT_HDR_SIZE +
			      RPMI_EVENT_DATALEN(ctx->p2a_buf[len / 4]);

		if (!IS_ALIGNED(next, 4) || next > hdr->datalen)
			break;
		len = next;
	}
	for (unsigned int i = 0; i < ctx->nr_sinks && len; i++)
		if (ctx->sinks[i].group == hdr->group)
			ctx->sinks[i].fn(ctx->sinks[i].ctx, ctx->p2a_buf, len);
}

void rpmi_poll(struct rpmi_context *ctx)
{
	struct rpmi_hdr hdr = {};

	if (!ctx || !ctx->has_p2a)
		return;

	context_lock(ctx);
	while (!ctx->recv(ctx, RPMI_QUEUE_P2A_REQ, &hdr, ctx->p2a_buf,
			  sizeof(ctx->p2a_buf))) {
		switch (hdr.flags & RPMI_FLAGS_TYPE_MASK) {
		case RPMI_MSG_NOTIFICATION:
			deliver_events(ctx, &hdr);
			break;
		case RPMI_MSG_NORMAL_REQUEST:
			/* No service is offered to the PuC: say so. */
			ctx->p2a_buf[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
			hdr.flags = RPMI_MSG_ACKNOWLEDGEMENT;
			hdr.datalen = 4;
			send_locked(ctx, RPMI_QUEUE_A2P_ACK, &hdr,
				    ctx->p2a_buf);
			break;
		default:
			break;
		}
	}
	context_unlock(ctx);
}
