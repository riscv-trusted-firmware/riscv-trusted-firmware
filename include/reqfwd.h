/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef REQFWD_H
#define REQFWD_H

/*
 * Messages forwarded from one domain to another (<domain.h>): what the
 * RPMI REQUEST_FORWARD service group is about when it is the SBI
 * implementation that forwards. A producer puts a message into the queue
 * of the target domain and waits for that domain to complete it, with a
 * response. The oldest message of a queue is its current one, the only
 * one that can be retrieved and completed.
 *
 * The queue moves bytes and knows nothing of what they mean. Producer and
 * target run on different harts, and a hart's M-mode is not at home in
 * another domain's memory: message and response live in the monitor's
 * memory, the producer's stack as a rule, which outlasts the wait.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct reqfwd_queue;

/*
 * The queue of domain 'key', NULL before anybody serves it (reqfwd_serve();
 * NULL there when two do already: a channel and a bridge is what a domain
 * may have). 'notify' is called, with no lock held and on the producer's hart,
 * when a message arrives in an empty queue.
 */
struct reqfwd_queue *reqfwd_queue_of(unsigned int key);
struct reqfwd_queue *reqfwd_serve(unsigned int key, void (*notify)(void *arg),
				  void *arg);

/*
 * Forward 'msg' and wait for its completion, at most 'timeout_us'. 0 and
 * the response in 'rsp' (*rsp_len bytes), or an RPMI error: RPMI_ERR_TIMEOUT
 * when the target domain did not complete it in time (it no longer can),
 * RPMI_ERR_BAD_RANGE when its response did not fit.
 */
int reqfwd_send(struct reqfwd_queue *q, const void *msg, size_t msg_len,
		void *rsp, size_t rsp_max, size_t *rsp_len,
		uint32_t timeout_us);

/*
 * The target domain's side. Retrieve: up to 'max' bytes of the current
 * message from 'start' on; RPMI_ERR_NO_DATA without a message,
 * RPMI_ERR_INVALID_PARAM for a start past its end. Complete: the current
 * message, which must have been retrieved (RPMI_ERR_NO_DATA); *left is the
 * number of messages that remain.
 */
int reqfwd_retrieve(struct reqfwd_queue *q, size_t start, void *buf, size_t max,
		    size_t *returned, size_t *remaining);
int reqfwd_complete(struct reqfwd_queue *q, const void *rsp, size_t rsp_len,
		    size_t *left);
/* How many messages wait in it. */
size_t reqfwd_count(struct reqfwd_queue *q);
/* The first bytes of the current message, for who announces it; 0: none. */
size_t reqfwd_peek(struct reqfwd_queue *q, void *buf, size_t max);

#endif
