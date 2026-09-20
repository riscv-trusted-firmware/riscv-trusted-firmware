// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Model of an RPMI platform microcontroller (PuC), just enough of one to
 * test the monitor's RPMI client, its shared memory transport, the MPXY
 * channels and the RPMI backends on a machine that has none: the BASE
 * service group, a clock service group with a few real services and test
 * services that make the PuC misbehave on request, and the system reset,
 * HSM and CPPC groups as the monitor's backends use them.
 *
 * It sits on the other end of the queues: it consumes the A2P request
 * queue and produces the P2A acknowledgment and P2A request queues.
 */

#include <atomic.h>
#include <io.h>
#include <rpmi.h>
#include <types_ext.h>
#include <util.h>

#include "sbitest.h"

#ifdef CONFIG_QEMU_VIRT_RPMI

#define SLOT_SIZE CONFIG_QEMU_VIRT_RPMI_SLOT_SIZE
#define QUEUE_SIZE CONFIG_QEMU_VIRT_RPMI_QUEUE_SIZE
#define MSG_SLOTS (QUEUE_SIZE / SLOT_SIZE - 2)
#define MAX_DATA (SLOT_SIZE - RPMI_MSG_HDR_SIZE)

uint32_t puc_hsm_starts, puc_hsm_stops, puc_hsm_last_hart;
uint32_t puc_hsm_suspends, puc_hsm_last_type;
uint64_t puc_hsm_last_addr;
uint32_t puc_hsm_refuse;
uint32_t puc_reset_queries;
uint32_t puc_posted_value;
uint32_t puc_notifications_enabled;
uint32_t puc_sysmsi_requests;

static uint32_t event_seq;

static vaddr_t queue_slot(enum rpmi_queue q, unsigned int slot)
{
	return CONFIG_QEMU_VIRT_RPMI_SHMEM_BASE + (vaddr_t)q * QUEUE_SIZE +
	       (vaddr_t)slot * SLOT_SIZE;
}

void puc_init(void)
{
	for (unsigned int q = 0; q < RPMI_QUEUE_COUNT; q++) {
		io_write32(queue_slot(q, 0), 0);
		io_write32(queue_slot(q, 1), 0);
	}
}

static void enqueue(enum rpmi_queue q, const struct rpmi_hdr *hdr,
		    const uint32_t *data)
{
	uint32_t tail = io_read32(queue_slot(q, 1));
	vaddr_t slot = queue_slot(q, 2 + tail);

	/* A full queue drops the message: the tests never get there. */
	if ((tail + 1) % MSG_SLOTS == io_read32(queue_slot(q, 0)))
		return;
	io_write32(slot, hdr->group | SHIFT_U32(hdr->service, 16) |
		   SHIFT_U32(hdr->flags, 24));
	io_write32(slot + 4, hdr->datalen | SHIFT_U32(hdr->token, 16));
	for (unsigned int i = 0; i < hdr->datalen / U(4); i++)
		io_write32(slot + 8 + 4 * i, data[i]);
	__asm__ __volatile__("fence rw, rw" ::: "memory");
	io_write32(queue_slot(q, 1), (tail + 1) % MSG_SLOTS);
}

/* SYSTEM_MSI state and target (address low, high, data) as they were set. */
static uint32_t sysmsi_state[PUC_NUM_SYSMSI], sysmsi_target[PUC_NUM_SYSMSI][3];
uint32_t puc_doorbell_rings;

/* The P2A doorbell: the system MSI the device tree says it is, when enabled. */
static void ring_p2a_doorbell(void)
{
	const uint32_t *t =
		sysmsi_target[CONFIG_QEMU_VIRT_RPMI_DOORBELL_SYSMSI];
	uint64_t addr = reg_pair_to_64(t[1], t[0]);

	if (!(sysmsi_state[CONFIG_QEMU_VIRT_RPMI_DOORBELL_SYSMSI] & 1) || !addr)
		return;
	atomic_inc32(&puc_doorbell_rings);
	io_write32((vaddr_t)addr, t[2]);
}

static void notify(uint16_t group, uint32_t count)
{
	struct rpmi_hdr hdr = {
		.group = group,
		.service = RPMI_SERVICE_NOTIFICATION,
		.flags = RPMI_MSG_NOTIFICATION,
	};
	uint32_t data[MAX_DATA / 4] = {};
	unsigned int n = 0;

	while (count--) {
		if ((n + 3) * 4 > MAX_DATA) {
			hdr.datalen = (uint16_t)(n * 4);
			enqueue(RPMI_QUEUE_P2A_REQ, &hdr, data);
			n = 0;
		}
		data[n++] = (PUC_EVENT_ID << 16) | PUC_EVENT_DATALEN;
		data[n++] = event_seq++;
		data[n++] = PUC_EVENT_MAGIC;
	}
	hdr.datalen = (uint16_t)(n * 4);
	if (n)
		enqueue(RPMI_QUEUE_P2A_REQ, &hdr, data);
	ring_p2a_doorbell();
}

/* PUC_TEST_NOTIFY_LATER: events the model sends of its own accord. */
static uint32_t later_count;
static uint16_t later_group;
static uint64_t later_at;

/* Fills 'resp' (STATUS first), returns its length in words; 0: no answer. */
static unsigned int serve_base(const struct rpmi_hdr *hdr, const uint32_t *req,
			       uint32_t *resp)
{
	resp[0] = RPMI_SUCCESS;
	switch (hdr->service) {
	case RPMI_BASE_GET_IMPL_VERSION:
		resp[1] = PUC_IMPL_VERSION;
		return 2;
	case RPMI_BASE_GET_IMPL_ID:
		resp[1] = PUC_IMPL_ID;
		return 2;
	case RPMI_BASE_GET_SPEC_VERSION:
		resp[1] = RPMI_VERSION(1, 0);
		return 2;
	case RPMI_BASE_PROBE_SERVICE_GROUP:
		resp[1] = req[0] == RPMI_GROUP_BASE ||
					  req[0] == RPMI_GROUP_CLOCK ||
					  req[0] == RPMI_GROUP_SYSTEM_MSI ||
					  req[0] == PUC_GROUP_TEST ?
				  RPMI_VERSION(1, 0) :
				  0;
		return 2;
	case RPMI_BASE_GET_ATTRIBUTES:
		resp[1] = 1; /* event notifications supported */
		resp[2] = 0;
		resp[3] = 0;
		resp[4] = 0;
		return 5;
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

#define CLOCK_GET_NUM_CLOCKS 0x02
#define CLOCK_SET_RATE 0x07
#define CLOCK_GET_RATE 0x08

static unsigned int serve_clock(const struct rpmi_hdr *hdr, const uint32_t *req,
				uint32_t *resp)
{
	static uint64_t rates[PUC_NUM_CLOCKS];
	unsigned int words = hdr->datalen / U(4);

	resp[0] = RPMI_SUCCESS;
	switch (hdr->service) {
	case CLOCK_GET_NUM_CLOCKS:
		resp[1] = PUC_NUM_CLOCKS;
		return 2;
	case CLOCK_GET_RATE:
	case CLOCK_SET_RATE:
		if (!words || req[0] >= PUC_NUM_CLOCKS) {
			resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
			return 1;
		}
		if (!rates[req[0]])
			rates[req[0]] = puc_clock_rate(req[0]);
		if (hdr->service == CLOCK_SET_RATE) {
			/* (CLOCK_ID, FLAGS, RATE_LOW, RATE_HIGH) */
			if (words < 4) {
				resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
				return 1;
			}
			rates[req[0]] = reg_pair_to_64(req[3], req[2]);
			return 1;
		}
		resp[1] = (uint32_t)rates[req[0]];
		resp[2] = high32_from_64(rates[req[0]]);
		return 3;
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

/*
 * SYSTEM_MSI: PUC_NUM_SYSMSI of them, state and target kept as they are
 * set. PUC_SYSMSI_MMODE prefers M-mode; the device tree makes another one
 * the P2A doorbell. What the monitor lets through shows in puc_sysmsi_*.
 */
static unsigned int serve_sysmsi(const struct rpmi_hdr *hdr,
				 const uint32_t *req, uint32_t *resp)
{
	uint32_t *state = sysmsi_state, (*target)[3] = sysmsi_target;

	resp[0] = RPMI_SUCCESS;
	if (hdr->service == RPMI_SYSMSI_GET_ATTRIBUTES) {
		resp[1] = PUC_NUM_SYSMSI;
		resp[2] = 0;
		resp[3] = 0;
		return 4;
	}
	if (hdr->service < RPMI_SYSMSI_GET_MSI_ATTRIBUTES ||
	    hdr->service > RPMI_SYSMSI_GET_MSI_TARGET) {
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
	if (hdr->datalen < 4 || req[0] >= PUC_NUM_SYSMSI) {
		resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
		return 1;
	}
	atomic_or_u32(&puc_sysmsi_requests, BIT32(req[0]));

	switch (hdr->service) {
	case RPMI_SYSMSI_GET_MSI_ATTRIBUTES:
		resp[1] = req[0] == PUC_SYSMSI_MMODE ?
				  RPMI_SYSMSI_FLAGS0_PREF_MMODE :
				  0;
		resp[2] = 0;
		resp[3] = 0x2d69736d; /* "msi-" */
		resp[4] = 0x30 + req[0];
		resp[5] = 0;
		resp[6] = 0;
		return 7;
	case RPMI_SYSMSI_SET_MSI_STATE:
		state[req[0]] = req[1];
		return 1;
	case RPMI_SYSMSI_GET_MSI_STATE:
		resp[1] = state[req[0]];
		return 2;
	case RPMI_SYSMSI_SET_MSI_TARGET:
		target[req[0]][0] = req[1];
		target[req[0]][1] = req[2];
		target[req[0]][2] = req[3];
		return 1;
	default:
		resp[1] = target[req[0]][0];
		resp[2] = target[req[0]][1];
		resp[3] = target[req[0]][2];
		return 4;
	}
}

/* The model's own group: services that make it misbehave on request. */
static unsigned int serve_test(const struct rpmi_hdr *hdr, const uint32_t *req,
			       uint32_t *resp)
{
	unsigned int words = hdr->datalen / U(4);

	resp[0] = RPMI_SUCCESS;
	switch (hdr->service) {
	case RPMI_SERVICE_ENABLE_NOTIFICATION:
		/* (EVENT_ID, REQ_STATE: 0 disable, 1 enable, 2 query) */
		if (words < 2 || req[0] != PUC_EVENT_ID || req[1] > 2) {
			resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
			return 1;
		}
		if (req[1] < 2)
			WRITE_ONCE(puc_notifications_enabled, req[1]);
		resp[1] = READ_ONCE(puc_notifications_enabled);
		return 2;
	case PUC_TEST_POSTED:
		WRITE_ONCE(puc_posted_value, words ? req[0] : 0);
		return 0;
	case PUC_TEST_SILENT:
		return 0;
	case PUC_TEST_NOTIFY:
		if (READ_ONCE(puc_notifications_enabled))
			notify(hdr->group, words ? req[0] : 0);
		return 1;
	case PUC_TEST_NOTIFY_LATER:
		later_count = words ? req[0] : 0;
		later_group = hdr->group;
		later_at = now() + TICKS_SHORT;
		return 1;
	case PUC_TEST_STALE_ACK:
	case PUC_TEST_ECHO:
		for (unsigned int i = 0; i < words && i + 1 < MAX_DATA / 4; i++)
			resp[1 + i] = req[i];
		return 1 + MIN(words, (unsigned int)(MAX_DATA / 4 - 1));
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

/* The M-mode only groups: what the monitor's RPMI backends talk to. */
static unsigned int serve_sysreset(const struct rpmi_hdr *hdr,
				   const uint32_t *req, uint32_t *resp)
{
	resp[0] = RPMI_SUCCESS;
	switch (hdr->service) {
	case RPMI_SYSRST_GET_ATTRIBUTES:
		atomic_inc32(&puc_reset_queries);
		resp[1] = req[0] == 0; /* shutdown only */
		return 2;
	case RPMI_SYSRST_RESET:
		if (req[0] == 0) {
			/*
			 * Nobody else prints now: the requester waits in
			 * M-mode.
			 */
			printf("puc: shutdown requested over RPMI\n");
			io_write32(CONFIG_RESET_SIFIVE_TEST_ADDR, 0x5555);
		}
		return 0;
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

static unsigned int serve_hsm(const struct rpmi_hdr *hdr, const uint32_t *req,
			      uint32_t *resp)
{
	resp[0] = RPMI_SUCCESS;
	switch (hdr->service) {
	case RPMI_HSM_GET_SUSPEND_TYPES:
		/* One at a time, to make the monitor come back for more. */
		if (req[0] >= 2) {
			resp[1] = 0;
			resp[2] = 0;
			return 3;
		}
		resp[1] = 1 - req[0];
		resp[2] = 1;
		resp[3] = req[0] ? PUC_SUSPEND_NON_RET : PUC_SUSPEND_RET;
		return 4;
	case RPMI_HSM_HART_SUSPEND:
		WRITE_ONCE(puc_hsm_last_hart, req[0]);
		WRITE_ONCE(puc_hsm_last_type, req[1]);
		puc_hsm_last_addr = reg_pair_to_64(req[3], req[2]);
		atomic_inc32(&puc_hsm_suspends);
		return 1;
	case RPMI_HSM_HART_START:
		if (READ_ONCE(puc_hsm_refuse)) {
			resp[0] = (uint32_t)RPMI_ERR_DENIED;
			return 1;
		}
		WRITE_ONCE(puc_hsm_last_hart, req[0]);
		puc_hsm_last_addr = reg_pair_to_64(req[2], req[1]);
		atomic_inc32(&puc_hsm_starts);
		return 1;
	case RPMI_HSM_HART_STOP:
		WRITE_ONCE(puc_hsm_last_hart, req[0]);
		atomic_inc32(&puc_hsm_stops);
		return 1;
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

/* Request and feedback channel of every hart, 8 bytes each; a power of two. */
static uint32_t cppc_fastchan[4096 / 4] __aligned(4096);
uint32_t puc_cppc_doorbell, puc_cppc_writes;

uint32_t *puc_cppc_fastchan(unsigned long hart)
{
	return &cppc_fastchan[4 * hart];
}

static unsigned int serve_cppc(const struct rpmi_hdr *hdr, const uint32_t *req,
			       uint32_t *resp)
{
	static uint64_t desired[SBITEST_MAX_HARTS];
	uint32_t reg = req[0], hart = req[1];

	resp[0] = RPMI_SUCCESS;
	if (hdr->service == RPMI_CPPC_GET_FAST_CHANNEL_REGION) {
		uint64_t base = (uintptr_t)cppc_fastchan,
			 db = (uintptr_t)&puc_cppc_doorbell;

		/* 32 bits wide, normal mode */
		resp[1] = RPMI_CPPC_FC_DOORBELL | (2 << 1);
		resp[2] = (uint32_t)base;
		resp[3] = high32_from_64(base);
		resp[4] = sizeof(cppc_fastchan);
		resp[5] = 0;
		resp[6] = (uint32_t)db;
		resp[7] = high32_from_64(db);
		resp[8] = PUC_CPPC_DB_VALUE;
		return 9;
	}
	if (hdr->service == RPMI_CPPC_GET_FAST_CHANNEL_OFFSET) {
		if (hdr->datalen < 4 || req[0] >= SBITEST_MAX_HARTS) {
			resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
			return 1;
		}
		resp[1] = 16 * req[0];
		resp[3] = 16 * req[0] + 8;
		resp[2] = 0;
		resp[4] = 0;
		return 5;
	}
	if (hdr->datalen < 8 || hart >= SBITEST_MAX_HARTS) {
		resp[0] = (uint32_t)RPMI_ERR_INVALID_PARAM;
		return 1;
	}
	if (reg != PUC_CPPC_REG_RO && reg != PUC_CPPC_REG_RW) {
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}

	switch (hdr->service) {
	case RPMI_CPPC_PROBE_REG:
		resp[1] = reg == PUC_CPPC_REG_RW ? 64 : 32;
		return 2;
	case RPMI_CPPC_READ_REG:
		/*
		 * A ring of the doorbell: the request channel has the latest
		 * word.
		 */
		if (READ_ONCE(puc_cppc_doorbell)) {
			WRITE_ONCE(puc_cppc_doorbell, 0);
			desired[hart] = READ_ONCE(*puc_cppc_fastchan(hart));
		}
		resp[1] = reg == PUC_CPPC_REG_RO ? PUC_CPPC_RO_VALUE :
						   (uint32_t)desired[hart];
		resp[2] = reg == PUC_CPPC_REG_RO ?
				  0 :
				  high32_from_64(desired[hart]);
		return 3;
	case RPMI_CPPC_WRITE_REG:
		if (reg == PUC_CPPC_REG_RO) {
			resp[0] = (uint32_t)RPMI_ERR_DENIED;
		} else {
			desired[hart] = reg_pair_to_64(req[3], req[2]);
			atomic_inc32(&puc_cppc_writes);
		}
		return 1;
	default:
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		return 1;
	}
}

void puc_poll(void)
{
	uint32_t head = io_read32(queue_slot(RPMI_QUEUE_A2P_REQ, 0));
	vaddr_t slot = 0;
	uint32_t req[MAX_DATA / 4] = {}, resp[MAX_DATA / 4] = {};
	struct rpmi_hdr hdr = {};
	unsigned int words = 0;

	if (later_count && now() >= later_at) {
		if (READ_ONCE(puc_notifications_enabled))
			notify(later_group, later_count);
		later_count = 0;
	}
	if (head == io_read32(queue_slot(RPMI_QUEUE_A2P_REQ, 1)))
		return;

	slot = queue_slot(RPMI_QUEUE_A2P_REQ, 2 + head);
	hdr = (struct rpmi_hdr){
		.group = (uint16_t)io_read32(slot),
		.service = (uint8_t)(io_read32(slot) >> 16),
		.flags = (uint8_t)(io_read32(slot) >> 24),
		.datalen = (uint16_t)io_read32(slot + 4),
		.token = (uint16_t)(io_read32(slot + 4) >> 16),
	};
	if (hdr.datalen > MAX_DATA)
		hdr.datalen = MAX_DATA;
	for (unsigned int i = 0; i < hdr.datalen / U(4); i++)
		req[i] = io_read32(slot + 8 + 4 * i);
	io_write32(queue_slot(RPMI_QUEUE_A2P_REQ, 0), (head + 1) % MSG_SLOTS);

	if (hdr.group == RPMI_GROUP_BASE) {
		words = serve_base(&hdr, req, resp);
	} else if (hdr.group == RPMI_GROUP_CLOCK) {
		words = serve_clock(&hdr, req, resp);
	} else if (hdr.group == RPMI_GROUP_SYSTEM_MSI) {
		words = serve_sysmsi(&hdr, req, resp);
	} else if (hdr.group == PUC_GROUP_TEST) {
		words = serve_test(&hdr, req, resp);
	} else if (hdr.group == RPMI_GROUP_SYSTEM_RESET) {
		words = serve_sysreset(&hdr, req, resp);
	} else if (hdr.group == RPMI_GROUP_HSM) {
		words = serve_hsm(&hdr, req, resp);
	} else if (hdr.group == RPMI_GROUP_CPPC) {
		words = serve_cppc(&hdr, req, resp);
	} else {
		resp[0] = (uint32_t)RPMI_ERR_NOT_SUPPORTED;
		words = 1;
	}

	/*
	 * Posted requests are not acknowledged, whatever the service made of
	 * them.
	 */
	if ((hdr.flags & RPMI_FLAGS_TYPE_MASK) != RPMI_MSG_NORMAL_REQUEST ||
	    !words)
		return;

	hdr.flags = RPMI_MSG_ACKNOWLEDGEMENT;
	hdr.datalen = (uint16_t)(words * 4);
	if (hdr.group == PUC_GROUP_TEST && hdr.service == PUC_TEST_STALE_ACK) {
		struct rpmi_hdr stale = hdr;

		stale.token = (uint16_t)(hdr.token + 1000);
		enqueue(RPMI_QUEUE_P2A_ACK, &stale, resp);
	}
	enqueue(RPMI_QUEUE_P2A_ACK, &hdr, resp);
}

#else

void puc_init(void)
{
}

void puc_poll(void)
{
}

#endif
