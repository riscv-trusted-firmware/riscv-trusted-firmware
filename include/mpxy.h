/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef MPXY_H
#define MPXY_H

/*
 * Message proxy channels (SBI MPXY): S-mode exchanges messages with a
 * message protocol implementation through a per-hart shared memory page.
 * The core owns the shared memory, the channel list and the standard
 * attributes; a channel's ops implement its message protocol. Return
 * values are SBI error codes.
 */

#include <compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util.h>

#define MPXY_SHMEM_SIZE UL(4096)

/* Standard channel attributes. */
#define MPXY_ATTR_MSG_PROT_ID 0x0
#define MPXY_ATTR_MSG_PROT_VERSION 0x1
#define MPXY_ATTR_MSG_DATA_MAX_LEN 0x2
#define MPXY_ATTR_MSG_SEND_TIMEOUT 0x3
#define MPXY_ATTR_MSG_COMPLETION_TIMEOUT 0x4
#define MPXY_ATTR_CHANNEL_CAPABILITY 0x5
#define MPXY_ATTR_SSE_EVENT_ID 0x6
#define MPXY_ATTR_MSI_CONTROL 0x7
#define MPXY_ATTR_MSI_ADDR_LOW 0x8
#define MPXY_ATTR_MSI_ADDR_HIGH 0x9
#define MPXY_ATTR_MSI_DATA 0xa
#define MPXY_ATTR_EVENTS_STATE_CONTROL 0xb
#define MPXY_ATTR_STD_COUNT 0xc
#define MPXY_ATTR_MSG_PROT_START U(0x80000000)

#define MPXY_CAP_MSI BIT(0)
#define MPXY_CAP_SSE BIT(1)
#define MPXY_CAP_EVENTS_STATE BIT(2)
#define MPXY_CAP_SEND_WITH_RESP BIT(3)
#define MPXY_CAP_SEND_WITHOUT_RESP BIT(4)
#define MPXY_CAP_GET_NOTIFICATIONS BIT(5)

#define MPXY_MSG_PROT_RPMI 0x0

struct mpxy_channel;

/* Notification events handed to S-mode by one get_notification_events call. */
struct mpxy_events {
	uint32_t returned;
	uint32_t remaining;
	uint32_t lost;
	unsigned long bytes;
};

struct mpxy_channel_ops {
	/* Message protocol attributes (MPXY_ATTR_MSG_PROT_START and up). */
	long (*read_attr)(struct mpxy_channel *ch, uint32_t id, uint32_t *val);
	long (*write_attr)(struct mpxy_channel *ch, uint32_t id, uint32_t val);
	/*
	 * 'buf' holds 'len' bytes of message data and receives up to
	 * 'resp_max' bytes of response; resp_len == NULL: no response wanted.
	 */
	long (*send)(struct mpxy_channel *ch, uint32_t msg_id, void *buf,
		     unsigned long len, unsigned long resp_max,
		     unsigned long *resp_len);
	/* Move pending events into 'buf' (at most 'max' bytes). */
	long (*get_events)(struct mpxy_channel *ch, void *buf,
			   unsigned long max, struct mpxy_events *ev);
};

struct mpxy_channel {
	uint32_t id;
	uint32_t msg_prot_id;
	uint32_t msg_prot_version;
	uint32_t msg_data_max_len;
	uint32_t send_timeout_us;
	uint32_t completion_timeout_us;
	uint32_t capability;
	uint32_t events_state_control;
	const struct mpxy_channel_ops *ops;
	struct mpxy_channel *next;
};

/* Boot time only. SBI_ERR_ALREADY_AVAILABLE when the id is taken. */
long mpxy_channel_register(struct mpxy_channel *ch);
unsigned int mpxy_channel_count(void);

/* The SBI calls, acting on the calling hart's shared memory. */
long mpxy_set_shmem(unsigned long lo, unsigned long hi, unsigned long flags);
long mpxy_get_channel_ids(unsigned long start_index);
long mpxy_read_attributes(unsigned long channel_id, unsigned long base,
			  unsigned long count);
long mpxy_write_attributes(unsigned long channel_id, unsigned long base,
			   unsigned long count);
long mpxy_send_message(unsigned long channel_id, unsigned long msg_id,
		       unsigned long len, unsigned long *resp_len);
long mpxy_get_notifications(unsigned long channel_id, unsigned long *bytes);

/*
 * RPMI over MPXY: bind 'channel_id' to one RPMI service group of the
 * platform microcontroller. Only groups the RPMI specification opens to
 * S-mode are accepted (SBI_ERR_DENIED otherwise). Boot time only.
 */
long mpxy_rpmi_channel_add(uint32_t channel_id, uint16_t group);

/* Calling hart (re)enters the next stage: no shared memory. */
#ifdef CONFIG_MPXY
void mpxy_hart_init(void);
#else
static inline void mpxy_hart_init(void)
{
}
#endif

#endif
