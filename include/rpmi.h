/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef RPMI_H
#define RPMI_H

/*
 * RISC-V Platform Management Interface (RPMI) v1.0, application processor
 * side: message format, identifiers, and the client used to talk to the
 * platform microcontroller (PuC) over a transport.
 *
 * Message data is little-endian, which is the byte order of every RISC-V
 * hart this firmware runs on: it is accessed as plain 32-bit words.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util.h>

#define RPMI_VERSION(major, minor) (SHIFT_U32(major, 16) | (minor))

/* Message header: two 32-bit words in front of the message data. */
#define RPMI_MSG_HDR_SIZE 8
#define RPMI_SLOT_SIZE_MIN 64

/* Header word 0: FLAGS[31:24] SERVICE_ID[23:16] SERVICEGROUP_ID[15:0] */
#define RPMI_FLAGS_TYPE_MASK 0x07
#define RPMI_FLAGS_DOORBELL 0x08 /* ring the P2A doorbell with the ack */

enum rpmi_msg_type {
	RPMI_MSG_NORMAL_REQUEST,
	RPMI_MSG_POSTED_REQUEST,
	RPMI_MSG_ACKNOWLEDGEMENT,
	RPMI_MSG_NOTIFICATION,
};

/* A message header, unpacked. */
struct rpmi_hdr {
	uint16_t group;
	uint8_t service;
	uint8_t flags;
	uint16_t datalen; /* bytes, a multiple of 4 */
	uint16_t token;
};

/* STATUS word of an acknowledgment; also the client's own error codes. */
#define RPMI_SUCCESS 0
#define RPMI_ERR_FAILED -1
#define RPMI_ERR_NOT_SUPPORTED -2
#define RPMI_ERR_INVALID_PARAM -3
#define RPMI_ERR_DENIED -4
#define RPMI_ERR_INVALID_ADDR -5
#define RPMI_ERR_ALREADY -6
#define RPMI_ERR_EXTENSION -7
#define RPMI_ERR_HW_FAULT -8
#define RPMI_ERR_BUSY -9
#define RPMI_ERR_INVALID_STATE -10
#define RPMI_ERR_BAD_RANGE -11
#define RPMI_ERR_TIMEOUT -12
#define RPMI_ERR_IO -13
#define RPMI_ERR_NO_DATA -14

/* Service groups. */
#define RPMI_GROUP_BASE 0x0001
#define RPMI_GROUP_SYSTEM_MSI 0x0002
#define RPMI_GROUP_SYSTEM_RESET 0x0003
#define RPMI_GROUP_SYSTEM_SUSPEND 0x0004
#define RPMI_GROUP_HSM 0x0005
#define RPMI_GROUP_CPPC 0x0006
#define RPMI_GROUP_VOLTAGE 0x0007
#define RPMI_GROUP_CLOCK 0x0008
#define RPMI_GROUP_DEVICE_POWER 0x0009
#define RPMI_GROUP_PERFORMANCE 0x000a
#define RPMI_GROUP_MANAGEMENT_MODE 0x000b
#define RPMI_GROUP_RAS_AGENT 0x000c
#define RPMI_GROUP_REQUEST_FORWARD 0x000d

/* Every group: service 0 is the notification, service 1 enables events. */
#define RPMI_SERVICE_NOTIFICATION 0x00
#define RPMI_SERVICE_ENABLE_NOTIFICATION 0x01

/* BASE service group. */
#define RPMI_BASE_GET_IMPL_VERSION 0x02
#define RPMI_BASE_GET_IMPL_ID 0x03
#define RPMI_BASE_GET_SPEC_VERSION 0x04
#define RPMI_BASE_GET_PLATFORM_INFO 0x05
#define RPMI_BASE_PROBE_SERVICE_GROUP 0x06
#define RPMI_BASE_GET_ATTRIBUTES 0x07

/* Event header inside a notification: EVENT_ID[23:16] EVENT_DATALEN[15:0] */
#define RPMI_EVENT_HDR_SIZE 4
#define RPMI_EVENT_DATALEN(hdr) ((hdr) & 0xffff)
#define RPMI_EVENT_ID(hdr) (((hdr) >> 16) & 0xff)

/*
 * Transport: moves whole messages between the application processors and
 * the PuC. The A2P channel (requests out, acknowledgments back) is
 * mandatory, the P2A channel (requests and notifications from the PuC)
 * optional.
 */
enum rpmi_queue {
	RPMI_QUEUE_A2P_REQ,
	RPMI_QUEUE_P2A_ACK,
	RPMI_QUEUE_P2A_REQ,
	RPMI_QUEUE_A2P_ACK,
	RPMI_QUEUE_COUNT,
};

struct rpmi_transport {
	const char *name;
	uint32_t max_data_len;
	bool has_p2a;
	/*
	 * 0, RPMI_ERR_BUSY when the queue is full, RPMI_ERR_NO_DATA when it
	 * is empty, RPMI_ERR_IO when the queue state makes no sense, and for
	 * recv RPMI_ERR_BAD_RANGE when the message was consumed but its data
	 * does not fit ('hdr' is valid, 'data' is not).
	 */
	int (*send)(enum rpmi_queue q, const struct rpmi_hdr *hdr,
		    const void *data);
	int (*recv)(enum rpmi_queue q, struct rpmi_hdr *hdr, void *data,
		    size_t max);
};

void rpmi_transport_register(const struct rpmi_transport *t);
bool rpmi_available(void);
const char *rpmi_transport_name(void);
uint32_t rpmi_max_data_len(void);

/*
 * Send a normal request and wait for its acknowledgment. 'resp' receives
 * the acknowledgment data, STATUS word first. 0 or an RPMI_ERR_* of the
 * client itself (TIMEOUT, IO, INVALID_PARAM); the service's own verdict is
 * the STATUS word.
 */
int rpmi_request(uint16_t group, uint8_t service, const void *req,
		 size_t req_len, void *resp, size_t resp_max, size_t *resp_len);
/* Send a posted request: nothing comes back. */
int rpmi_post(uint16_t group, uint8_t service, const void *req, size_t req_len);

/* BASE group: 0 or a negative RPMI error, transport or STATUS alike. */
int rpmi_base_get(uint8_t service, uint32_t *value);
/* version 0: the PuC does not implement the group. */
int rpmi_probe_group(uint16_t group, uint32_t *version);

/*
 * Notifications. A sink receives the events of one service group (whole
 * events, packed as in the notification message). rpmi_poll() drains the
 * P2A request queue: notifications go to their sink, requests from the PuC
 * are answered RPMI_ERR_NOT_SUPPORTED.
 */
typedef void (*rpmi_event_sink_t)(void *ctx, const void *events, size_t len);

int rpmi_event_sink_register(uint16_t group, rpmi_event_sink_t sink, void *ctx);
void rpmi_poll(void);

#endif
