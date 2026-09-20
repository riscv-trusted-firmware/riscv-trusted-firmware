// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI shared memory transport. Four circular queues of equal-sized slots:
 * slot 0 holds the head index, slot 1 the tail index, the others one
 * message each. The consumer owns the head, the producer the tail; the PuC
 * initialises the queues. An optional memory-mapped doorbell tells the PuC
 * about new messages.
 *
 * The geometry comes from Kconfig: the queues are contiguous, in the order
 * A2P request, P2A acknowledgment, P2A request, A2P acknowledgment.
 *
 * The other side is not this firmware: every index and length read from
 * the shared memory is checked before it is used.
 */

#include <driver.h>
#include <io.h>
#include <rpmi.h>
#include <string.h>
#include <types_ext.h>
#include <util.h>

#define SLOT_SIZE CONFIG_RPMI_SHMEM_SLOT_SIZE
#define QUEUE_SIZE CONFIG_RPMI_SHMEM_QUEUE_SIZE
#define MSG_SLOTS (QUEUE_SIZE / SLOT_SIZE - 2)

_Static_assert(IS_POWER_OF_TWO(SLOT_SIZE) && SLOT_SIZE >= RPMI_SLOT_SIZE_MIN,
	       "RPMI slot size: a power of two, at least 64 bytes");
_Static_assert(QUEUE_SIZE % SLOT_SIZE == 0 && QUEUE_SIZE / SLOT_SIZE >= 3,
	       "RPMI queue size: a multiple of the slot size, 3 slots or more");

static vaddr_t queue_slot(enum rpmi_queue q, unsigned int slot)
{
	return CONFIG_RPMI_SHMEM_BASE + (vaddr_t)q * QUEUE_SIZE +
	       (vaddr_t)slot * SLOT_SIZE;
}

static int rpmi_shmem_send(enum rpmi_queue q, const struct rpmi_hdr *hdr,
			   const void *data)
{
	uint32_t head = io_read32(queue_slot(q, 0)),
		 tail = io_read32(queue_slot(q, 1));
	vaddr_t slot = 0;
	const uint32_t *words = data;

	if (q != RPMI_QUEUE_A2P_REQ && q != RPMI_QUEUE_A2P_ACK)
		return RPMI_ERR_INVALID_PARAM;
	if (head >= MSG_SLOTS || tail >= MSG_SLOTS)
		return RPMI_ERR_IO;
	if ((tail + 1) % MSG_SLOTS == head)
		return RPMI_ERR_BUSY;
	if (hdr->datalen > SLOT_SIZE - RPMI_MSG_HDR_SIZE)
		return RPMI_ERR_INVALID_PARAM;

	slot = queue_slot(q, 2 + tail);
	io_write32(slot, hdr->group | SHIFT_U32(hdr->service, 16) |
		   SHIFT_U32(hdr->flags, 24));
	io_write32(slot + 4, hdr->datalen | SHIFT_U32(hdr->token, 16));
	for (unsigned int i = 0; i < hdr->datalen / U(4); i++)
		io_write32(slot + 8 + 4 * i, words[i]);

	/* The message is complete before the tail makes it visible. */
	__asm__ __volatile__("fence rw, rw" ::: "memory");
	io_write32(queue_slot(q, 1), (tail + 1) % MSG_SLOTS);

	if (CONFIG_RPMI_SHMEM_DOORBELL_ADDR)
		io_write32(CONFIG_RPMI_SHMEM_DOORBELL_ADDR,
			   CONFIG_RPMI_SHMEM_DOORBELL_VALUE);
	return RPMI_SUCCESS;
}

static int rpmi_shmem_recv(enum rpmi_queue q, struct rpmi_hdr *hdr, void *data,
			   size_t max)
{
	uint32_t head = io_read32(queue_slot(q, 0)),
		 tail = io_read32(queue_slot(q, 1));
	vaddr_t slot = 0;
	uint32_t *words = data;
	uint32_t w0 = 0, w1 = 0;
	int rc = RPMI_SUCCESS;

	if (q != RPMI_QUEUE_P2A_ACK && q != RPMI_QUEUE_P2A_REQ)
		return RPMI_ERR_INVALID_PARAM;
	if (head >= MSG_SLOTS || tail >= MSG_SLOTS)
		return RPMI_ERR_IO;
	if (head == tail)
		return RPMI_ERR_NO_DATA;

	slot = queue_slot(q, 2 + head);
	w0 = io_read32(slot);
	w1 = io_read32(slot + 4);
	*hdr = (struct rpmi_hdr){
		.group = (uint16_t)w0,
		.service = (uint8_t)(w0 >> 16),
		.flags = (uint8_t)(w0 >> 24),
		.datalen = (uint16_t)w1,
		.token = (uint16_t)(w1 >> 16),
	};

	/*
	 * A message that does not fit is consumed and reported, header valid.
	 */
	if (!IS_ALIGNED(hdr->datalen, 4) ||
	    hdr->datalen > SLOT_SIZE - RPMI_MSG_HDR_SIZE || hdr->datalen > max)
		rc = RPMI_ERR_BAD_RANGE;
	else
		for (unsigned int i = 0; i < hdr->datalen / U(4); i++)
			words[i] = io_read32(slot + 8 + 4 * i);

	io_write32(queue_slot(q, 0), (head + 1) % MSG_SLOTS);
	return rc;
}

static const struct rpmi_transport rpmi_shmem_transport = {
	.name = "rpmi-shmem",
	.max_data_len = SLOT_SIZE - RPMI_MSG_HDR_SIZE,
	.has_p2a = true,
	.send = rpmi_shmem_send,
	.recv = rpmi_shmem_recv,
};

static int rpmi_shmem_probe(const void *fdt)
{
	rpmi_transport_register(&rpmi_shmem_transport);
	return 0;
}

DRIVER_DEFINE(rpmi_shmem) = {
	.name = "rpmi-shmem",
	.probe = rpmi_shmem_probe,
};
