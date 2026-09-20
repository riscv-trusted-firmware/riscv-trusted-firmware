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

/* SYSTEM_RESET, SYSTEM_SUSPEND, HSM and CPPC service groups. */
#define RPMI_SYSRST_GET_ATTRIBUTES 0x02
#define RPMI_SYSRST_RESET 0x03
#define RPMI_SYSSUSP_GET_ATTRIBUTES 0x02
#define RPMI_SYSSUSP_SUSPEND 0x03
#define RPMI_HSM_GET_SUSPEND_TYPES 0x04
#define RPMI_HSM_HART_START 0x06
#define RPMI_HSM_HART_STOP 0x07
#define RPMI_HSM_HART_SUSPEND 0x08
#define RPMI_CPPC_PROBE_REG 0x02
#define RPMI_CPPC_READ_REG 0x03
#define RPMI_CPPC_WRITE_REG 0x04
#define RPMI_CPPC_GET_FAST_CHANNEL_REGION 0x05
#define RPMI_CPPC_GET_FAST_CHANNEL_OFFSET 0x06
/* FLAGS of GET_FAST_CHANNEL_REGION */
#define RPMI_CPPC_FC_DOORBELL 0x1
#define RPMI_CPPC_FC_DB_WIDTH(flags) \
	(((flags) >> 1) & 3) /* 0, 1, 2: 8, 16, 32 bits */
#define RPMI_CPPC_FC_MODE(flags) \
	(((flags) >> 3) & 3) /* 0: normal, 1: autonomous */

/* SYSTEM_MSI service group. */
#define RPMI_SYSMSI_GET_ATTRIBUTES 0x02
#define RPMI_SYSMSI_GET_MSI_ATTRIBUTES 0x03
#define RPMI_SYSMSI_SET_MSI_STATE 0x04
#define RPMI_SYSMSI_GET_MSI_STATE 0x05
#define RPMI_SYSMSI_SET_MSI_TARGET 0x06
#define RPMI_SYSMSI_GET_MSI_TARGET 0x07
/* GET_MSI_ATTRIBUTES FLAGS0: M-mode is where this MSI wants to be handled. */
#define RPMI_SYSMSI_FLAGS0_PREF_MMODE 0x1

/* Event header inside a notification: EVENT_ID[23:16] EVENT_DATALEN[15:0] */
#define RPMI_EVENT_HDR_SIZE 4
#define RPMI_EVENT_DATALEN(hdr) ((hdr) & 0xffff)
#define RPMI_EVENT_ID(hdr) (((hdr) >> 16) & 0xff)

/* The protocol above is for anyone; the client needs CONFIG_RPMI. */
#ifdef CONFIG_RPMI

/*
 * A context is one transport instance to one PuC, with the client state
 * that goes with it. A platform can have several; a user names its own by
 * the phandle in its "mboxes" property.
 *
 * The transport moves whole messages between the application processors
 * and the PuC. The A2P channel (requests out, acknowledgments back) is
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

#define RPMI_MAX_EVENT_SINKS 8
#define RPMI_NO_SYSMSI (~U(0))

typedef void (*rpmi_event_sink_t)(void *ctx, const void *events, size_t len);

struct rpmi_context {
	/* Filled in by the transport driver. */
	const char *name;
	uint32_t phandle; /* of the device tree node behind it */
	uint32_t max_data_len;
	bool has_p2a;
	/* The system MSI that is the P2A doorbell, RPMI_NO_SYSMSI without. */
	uint32_t p2a_doorbell_sysmsi;
	void *priv;
	/*
	 * 0, RPMI_ERR_BUSY when the queue is full, RPMI_ERR_NO_DATA when it
	 * is empty, RPMI_ERR_IO when the queue state makes no sense, and for
	 * recv RPMI_ERR_BAD_RANGE when the message was consumed but its data
	 * does not fit ('hdr' is valid, 'data' is not).
	 */
	int (*send)(struct rpmi_context *ctx, enum rpmi_queue q,
		    const struct rpmi_hdr *hdr, const void *data);
	int (*recv)(struct rpmi_context *ctx, enum rpmi_queue q,
		    struct rpmi_hdr *hdr, void *data, size_t max);

	/* The client's. */
	unsigned long lock;
	uint16_t next_token;
	unsigned int nr_sinks;
	struct {
		uint16_t group;
		rpmi_event_sink_t fn;
		void *ctx;
	} sinks[RPMI_MAX_EVENT_SINKS];
	uint32_t p2a_buf[CONFIG_RPMI_MAX_DATA_LEN / 4];
	struct rpmi_context *next;
};

void rpmi_context_register(struct rpmi_context *ctx);
struct rpmi_context *rpmi_context_find(uint32_t phandle);
/* For the boot log: the transports there are. */
unsigned int rpmi_context_count(void);
uint32_t rpmi_max_data_len(const struct rpmi_context *ctx);

/*
 * A user of RPMI services in the device tree: "mboxes = <&transport
 * service-group>". 0, or RPMI_ERR_* when the node has no such property or
 * the transport it names was not probed.
 */
int rpmi_client_from_fdt(const void *fdt, int node, struct rpmi_context **ctx,
			 uint16_t *group);

/*
 * Send a normal request and wait for its acknowledgment. 'resp' receives
 * the acknowledgment data, STATUS word first. 0 or an RPMI_ERR_* of the
 * client itself (TIMEOUT, IO, INVALID_PARAM); the service's own verdict is
 * the STATUS word.
 */
int rpmi_request(struct rpmi_context *ctx, uint16_t group, uint8_t service,
		 const void *req, size_t req_len, void *resp, size_t resp_max,
		 size_t *resp_len);
/* Send a posted request: nothing comes back. */
int rpmi_post(struct rpmi_context *ctx, uint16_t group, uint8_t service,
	      const void *req, size_t req_len);

/*
 * A normal request with 'req_words' words in and up to 'resp_words' words
 * out, STATUS first. 0, or a negative RPMI error: the client's own, else
 * the STATUS of the acknowledgment. A short acknowledgment is RPMI_ERR_IO.
 */
int rpmi_call(struct rpmi_context *ctx, uint16_t group, uint8_t service,
	      const uint32_t *req, unsigned int req_words, uint32_t *resp,
	      unsigned int resp_words);

/* BASE group: 0 or a negative RPMI error, transport or STATUS alike. */
int rpmi_base_get(struct rpmi_context *ctx, uint8_t service, uint32_t *value);
/* version 0: the PuC does not implement the group. */
int rpmi_probe_group(struct rpmi_context *ctx, uint16_t group,
		     uint32_t *version);

/*
 * Notifications. A sink receives the events of one service group (whole
 * events, packed as in the notification message). rpmi_poll() drains the
 * P2A request queue: notifications go to their sink, requests from the PuC
 * are answered RPMI_ERR_NOT_SUPPORTED.
 */
int rpmi_event_sink_register(struct rpmi_context *ctx, uint16_t group,
			     rpmi_event_sink_t sink, void *sink_ctx);
void rpmi_poll(struct rpmi_context *ctx);

#endif /* CONFIG_RPMI */

#endif
