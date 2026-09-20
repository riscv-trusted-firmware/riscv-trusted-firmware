// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI client: one transport, one request in flight. The requester holds
 * the lock from the enqueue until its acknowledgment shows up (or the
 * timeout expires) and polls for it; acknowledgments carrying another
 * token are leftovers (an earlier timeout, a previous firmware) and are
 * dropped.
 */

#include <ipi.h>
#include <log.h>
#include <rpmi.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <util.h>

#define MAX_EVENT_SINKS 8

struct event_sink {
	uint16_t group;
	rpmi_event_sink_t fn;
	void *ctx;
};

static const struct rpmi_transport *transport;
static unsigned long rpmi_lock = SPINLOCK_UNLOCK;
static uint16_t next_token;
static struct event_sink sinks[MAX_EVENT_SINKS];
static unsigned int nr_sinks;

/* Bounce buffer for P2A requests and notifications, under rpmi_lock. */
static uint32_t p2a_buf[CONFIG_RPMI_MAX_DATA_LEN / 4];

void rpmi_transport_register(const struct rpmi_transport *t)
{
	transport = t;
}

bool rpmi_available(void)
{
	return !!transport;
}

const char *rpmi_transport_name(void)
{
	return transport ? transport->name : "none";
}

uint32_t rpmi_max_data_len(void)
{
	if (!transport)
		return 0;
	return transport->max_data_len < CONFIG_RPMI_MAX_DATA_LEN ?
		       transport->max_data_len :
		       CONFIG_RPMI_MAX_DATA_LEN;
}

/* Waiting for another hart or the PuC: keep serving our own IPIs. */
static void rpmi_lock_acquire(void)
{
	while (!spin_trylock(&rpmi_lock))
		ipi_process();
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
static int send_locked(enum rpmi_queue q, const struct rpmi_hdr *hdr,
		       const void *data)
{
	uint64_t until = deadline();
	int rc = 0;

	while ((rc = transport->send(q, hdr, data)) == RPMI_ERR_BUSY) {
		if (expired(until))
			return RPMI_ERR_TIMEOUT;
		ipi_process();
		cpu_relax();
	}
	return rc;
}

static int request_check(size_t req_len)
{
	if (!transport)
		return RPMI_ERR_NOT_SUPPORTED;
	if (!IS_ALIGNED(req_len, 4) || req_len > rpmi_max_data_len())
		return RPMI_ERR_INVALID_PARAM;
	return RPMI_SUCCESS;
}

/* Poll for the acknowledgment of 'req'; called with the lock held. */
static int wait_ack(const struct rpmi_hdr *req, void *resp, size_t resp_max,
		    size_t *resp_len)
{
	uint64_t until = deadline();
	struct rpmi_hdr ack = {};

	for (;;) {
		int rc = transport->recv(RPMI_QUEUE_P2A_ACK, &ack, resp,
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

int rpmi_request(uint16_t group, uint8_t service, const void *req,
		 size_t req_len, void *resp, size_t resp_max, size_t *resp_len)
{
	struct rpmi_hdr hdr = {
		.group = group,
		.service = service,
		.flags = RPMI_MSG_NORMAL_REQUEST,
		.datalen = (uint16_t)req_len,
	};
	int rc = request_check(req_len);

	if (rc)
		return rc;

	rpmi_lock_acquire();
	hdr.token = next_token++;
	rc = send_locked(RPMI_QUEUE_A2P_REQ, &hdr, req);
	if (!rc)
		rc = wait_ack(&hdr, resp, resp_max, resp_len);
	spin_unlock(&rpmi_lock);
	return rc;
}

int rpmi_post(uint16_t group, uint8_t service, const void *req, size_t req_len)
{
	struct rpmi_hdr hdr = {
		.group = group,
		.service = service,
		.flags = RPMI_MSG_POSTED_REQUEST,
		.datalen = (uint16_t)req_len,
	};
	int rc = request_check(req_len);

	if (rc)
		return rc;

	rpmi_lock_acquire();
	hdr.token = next_token++;
	rc = send_locked(RPMI_QUEUE_A2P_REQ, &hdr, req);
	spin_unlock(&rpmi_lock);
	return rc;
}

int rpmi_call(uint16_t group, uint8_t service, const uint32_t *req,
	      unsigned int req_words, uint32_t *resp, unsigned int resp_words)
{
	size_t len = 0;
	int rc = 0;

	rc = rpmi_request(group, service, req, 4 * req_words, resp,
			  4 * resp_words, &len);
	if (rc)
		return rc;
	if ((int32_t)resp[0])
		return (int32_t)resp[0];
	return len < 4 * resp_words ? RPMI_ERR_IO : RPMI_SUCCESS;
}

/* A BASE service without request data that returns (STATUS, value). */
int rpmi_base_get(uint8_t service, uint32_t *value)
{
	uint32_t resp[2] = { 0 };
	int rc = rpmi_call(RPMI_GROUP_BASE, service, NULL, 0, resp, 2);

	if (!rc)
		*value = resp[1];
	return rc;
}

int rpmi_probe_group(uint16_t group, uint32_t *version)
{
	uint32_t req = group, resp[2] = { 0 };
	int rc = rpmi_call(RPMI_GROUP_BASE, RPMI_BASE_PROBE_SERVICE_GROUP, &req,
			   1, resp, 2);

	if (!rc)
		*version = resp[1];
	return rc;
}

int rpmi_event_sink_register(uint16_t group, rpmi_event_sink_t sink, void *ctx)
{
	if (nr_sinks == MAX_EVENT_SINKS)
		return RPMI_ERR_FAILED;
	sinks[nr_sinks++] = (struct event_sink){ group, sink, ctx };
	return RPMI_SUCCESS;
}

/*
 * Hand over the well-formed events of a notification; stop at the first bad
 * one.
 */
static void deliver_events(const struct rpmi_hdr *hdr)
{
	size_t len = 0;

	while (len + RPMI_EVENT_HDR_SIZE <= hdr->datalen) {
		size_t next = len + RPMI_EVENT_HDR_SIZE +
			      RPMI_EVENT_DATALEN(p2a_buf[len / 4]);

		if (!IS_ALIGNED(next, 4) || next > hdr->datalen)
			break;
		len = next;
	}
	for (unsigned int i = 0; i < nr_sinks && len; i++)
		if (sinks[i].group == hdr->group)
			sinks[i].fn(sinks[i].ctx, p2a_buf, len);
}

void rpmi_poll(void)
{
	struct rpmi_hdr hdr = {};

	if (!transport || !transport->has_p2a)
		return;

	rpmi_lock_acquire();
	while (!transport->recv(RPMI_QUEUE_P2A_REQ, &hdr, p2a_buf,
				sizeof(p2a_buf))) {
		switch (hdr.flags & RPMI_FLAGS_TYPE_MASK) {
		case RPMI_MSG_NOTIFICATION:
			deliver_events(&hdr);
			break;
		case RPMI_MSG_NORMAL_REQUEST:
			/* No service is offered to the PuC: say so. */
			p2a_buf[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
			hdr.flags = RPMI_MSG_ACKNOWLEDGEMENT;
			hdr.datalen = 4;
			send_locked(RPMI_QUEUE_A2P_ACK, &hdr, p2a_buf);
			break;
		default:
			break;
		}
	}
	spin_unlock(&rpmi_lock);
}
