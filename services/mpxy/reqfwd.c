// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* Queues of messages forwarded between domains, see <reqfwd.h>. */

#include <arch/hart.h>
#include <atomic.h>
#include <domain.h>
#include <ipi.h>
#include <mpxy.h>
#include <reqfwd.h>
#include <rpmi.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <util.h>

enum { QUEUED, RETRIEVED, COMPLETED, FAILED };

struct reqfwd_message {
	struct reqfwd_message *next;
	/* atomic once it is COMPLETED or FAILED */
	unsigned long state;
	const void *msg;
	size_t msg_len;
	void *rsp;
	size_t rsp_max, rsp_len;
};

struct reqfwd_queue {
	/* the list and the states of its messages */
	unsigned long lock;
	bool served;
	struct reqfwd_message *head, **tail;
	size_t count;
	void (*notify)(void *arg);
	void *arg;
};

static struct reqfwd_queue queues[DOMAIN_KEYS];

struct reqfwd_queue *reqfwd_queue_of(unsigned int key)
{
	return key < DOMAIN_KEYS && queues[key].served ? &queues[key] : NULL;
}

struct reqfwd_queue *reqfwd_serve(unsigned int key, void (*notify)(void *arg),
				  void *arg)
{
	struct reqfwd_queue *q = &queues[key];

	if (key >= DOMAIN_KEYS || q->served)
		return NULL;
	q->tail = &q->head;
	q->notify = notify;
	q->arg = arg;
	q->served = true;
	return q;
}

/* Out of the queue, wherever it is; called with the lock held. */
static void unlink(struct reqfwd_queue *q, struct reqfwd_message *m)
{
	struct reqfwd_message **p = &q->head;

	while (*p && *p != m)
		p = &(*p)->next;
	if (!*p)
		return;
	*p = m->next;
	if (q->tail == &m->next)
		q->tail = p;
	q->count--;
}

int reqfwd_send(struct reqfwd_queue *q, const void *msg, size_t msg_len,
		void *rsp, size_t rsp_max, size_t *rsp_len, uint32_t timeout_us)
{
	struct reqfwd_message m = {
		.state = QUEUED,
		.msg = msg,
		.msg_len = msg_len,
		.rsp = rsp,
		.rsp_max = rsp_max,
	};
	uint64_t until = timer_now() + timer_usecs_to_ticks(timeout_us);
	unsigned long state = 0;
	bool first = false;

	spin_lock(&q->lock);
	first = !q->count++;
	*q->tail = &m;
	q->tail = &m.next;
	spin_unlock(&q->lock);

	/* A domain that works through its queue gets to this one by itself. */
	if (first && q->notify)
		q->notify(q->arg);

	while ((state = atomic_load_ulong(&m.state)) < COMPLETED) {
		if (timer_available() && timer_now() > until) {
			/*
			 * Gone before the lock goes: a late completion finds
			 * nothing.
			 */
			spin_lock(&q->lock);
			state = m.state;
			if (state < COMPLETED)
				unlink(q, &m);
			spin_unlock(&q->lock);
			if (state < COMPLETED)
				return RPMI_ERR_TIMEOUT;
			break;
		}
		/*
		 * An M-mode wait like any other; and the target may need
		 * telling.
		 */
		ipi_process();
		mpxy_indicate();
		cpu_relax();
	}
	*rsp_len = m.rsp_len;
	return state == COMPLETED ? RPMI_SUCCESS : RPMI_ERR_BAD_RANGE;
}

int reqfwd_retrieve(struct reqfwd_queue *q, size_t start, void *buf, size_t max,
		    size_t *returned, size_t *remaining)
{
	struct reqfwd_message *m = NULL;
	int rc = RPMI_SUCCESS;

	spin_lock(&q->lock);
	m = q->head;
	if (!m) {
		rc = RPMI_ERR_NO_DATA;
	} else if (start >= m->msg_len) {
		rc = RPMI_ERR_INVALID_PARAM;
	} else {
		*returned = MIN(m->msg_len - start, max);
		*remaining = m->msg_len - start - *returned;
		if (*returned)
			memcpy(buf, (const char *)m->msg + start, *returned);
		m->state = RETRIEVED;
	}
	spin_unlock(&q->lock);
	return rc;
}

int reqfwd_complete(struct reqfwd_queue *q, const void *rsp, size_t rsp_len,
		    size_t *left)
{
	struct reqfwd_message *m = NULL;
	int rc = RPMI_SUCCESS;

	spin_lock(&q->lock);
	m = q->head;
	if (!m || m->state != RETRIEVED) {
		rc = RPMI_ERR_NO_DATA;
	} else {
		bool fits = rsp_len <= m->rsp_max;

		/*
		 * Out of the queue first: the producer's stack is gone right
		 * after.
		 */
		unlink(q, m);
		if (fits && rsp_len)
			memcpy(m->rsp, rsp, rsp_len);
		m->rsp_len = fits ? rsp_len : 0;
		atomic_store_ulong(&m->state, fits ? COMPLETED : FAILED);
		if (!fits)
			rc = RPMI_ERR_BAD_RANGE;
	}
	*left = q->count;
	spin_unlock(&q->lock);
	return rc;
}

size_t reqfwd_peek(struct reqfwd_queue *q, void *buf, size_t max)
{
	size_t len = 0;

	spin_lock(&q->lock);
	if (q->head) {
		len = MIN(q->head->msg_len, max);
		memcpy(buf, q->head->msg, len);
	}
	spin_unlock(&q->lock);
	return len;
}
