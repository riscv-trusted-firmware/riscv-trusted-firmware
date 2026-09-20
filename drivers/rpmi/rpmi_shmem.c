// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RPMI shared memory transport ("riscv,rpmi-shmem-mbox"). Four circular
 * queues of equal-sized slots: slot 0 holds the head index, slot 1 the tail
 * index, the others one message each. The consumer owns the head, the
 * producer the tail; the PuC initialises the queues. An optional
 * memory-mapped doorbell tells the PuC about new messages.
 *
 * The node names the queues in "reg-names": "a2p-req" and "p2a-ack" (the
 * A2P channel, mandatory), "p2a-req" and "a2p-ack" (the P2A channel), and
 * "a2p-doorbell"; "riscv,slot-size" and "riscv,a2p-doorbell-value" go with
 * them.
 *
 * The other side is not this firmware: every index and length read from
 * the shared memory is checked before it is used.
 */

#include <driver.h>
#include <fdt_util.h>
#include <io.h>
#include <log.h>
#include <memregion.h>
#include <rpmi.h>
#include <string.h>
#include <types_ext.h>
#include <util.h>

struct rpmi_shmem {
	struct rpmi_context ctx;
	vaddr_t queue[RPMI_QUEUE_COUNT]; /* 0: not there */
	uint32_t msg_slots[RPMI_QUEUE_COUNT];
	uint32_t slot_size;
	vaddr_t doorbell; /* 0: none */
	uint32_t doorbell_value;
};

#ifdef CONFIG_RPMI_SHMEM_PROTECT
#define IS_ENABLED_RPMI_SHMEM_PROTECT 1
#else
#define IS_ENABLED_RPMI_SHMEM_PROTECT 0
#endif

static struct rpmi_shmem instances[CONFIG_RPMI_SHMEM_MAX_INSTANCES];
static unsigned int nr_instances;

static const char *const queue_names[RPMI_QUEUE_COUNT] = {
	[RPMI_QUEUE_A2P_REQ] = "a2p-req",
	[RPMI_QUEUE_P2A_ACK] = "p2a-ack",
	[RPMI_QUEUE_P2A_REQ] = "p2a-req",
	[RPMI_QUEUE_A2P_ACK] = "a2p-ack",
};

static vaddr_t queue_slot(const struct rpmi_shmem *t, enum rpmi_queue q,
			  unsigned int slot)
{
	return t->queue[q] + (vaddr_t)slot * t->slot_size;
}

static int rpmi_shmem_send(struct rpmi_context *ctx, enum rpmi_queue q,
			   const struct rpmi_hdr *hdr, const void *data)
{
	const struct rpmi_shmem *t = ctx->priv;
	const uint32_t *words = data;
	vaddr_t slot = 0;
	uint32_t head = 0, tail = 0;

	if ((q != RPMI_QUEUE_A2P_REQ && q != RPMI_QUEUE_A2P_ACK) ||
	    !t->queue[q])
		return RPMI_ERR_INVALID_PARAM;
	head = io_read32(queue_slot(t, q, 0));
	tail = io_read32(queue_slot(t, q, 1));
	if (head >= t->msg_slots[q] || tail >= t->msg_slots[q])
		return RPMI_ERR_IO;
	if ((tail + 1) % t->msg_slots[q] == head)
		return RPMI_ERR_BUSY;
	if (hdr->datalen > t->slot_size - RPMI_MSG_HDR_SIZE)
		return RPMI_ERR_INVALID_PARAM;

	slot = queue_slot(t, q, 2 + tail);
	io_write32(slot, hdr->group | SHIFT_U32(hdr->service, 16) |
		   SHIFT_U32(hdr->flags, 24));
	io_write32(slot + 4, hdr->datalen | SHIFT_U32(hdr->token, 16));
	for (unsigned int i = 0; i < hdr->datalen / U(4); i++)
		io_write32(slot + 8 + 4 * i, words[i]);

	/* The message is complete before the tail makes it visible. */
	__asm__ __volatile__("fence rw, rw" ::: "memory");
	io_write32(queue_slot(t, q, 1), (tail + 1) % t->msg_slots[q]);

	if (t->doorbell)
		io_write32(t->doorbell, t->doorbell_value);
	return RPMI_SUCCESS;
}

static int rpmi_shmem_recv(struct rpmi_context *ctx, enum rpmi_queue q,
			   struct rpmi_hdr *hdr, void *data, size_t max)
{
	const struct rpmi_shmem *t = ctx->priv;
	vaddr_t slot = 0;
	uint32_t *words = data;
	uint32_t head = 0, tail = 0, w0 = 0, w1 = 0;
	int rc = RPMI_SUCCESS;

	if ((q != RPMI_QUEUE_P2A_ACK && q != RPMI_QUEUE_P2A_REQ) ||
	    !t->queue[q])
		return RPMI_ERR_INVALID_PARAM;
	head = io_read32(queue_slot(t, q, 0));
	tail = io_read32(queue_slot(t, q, 1));
	if (head >= t->msg_slots[q] || tail >= t->msg_slots[q])
		return RPMI_ERR_IO;
	if (head == tail)
		return RPMI_ERR_NO_DATA;

	slot = queue_slot(t, q, 2 + head);
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
	    hdr->datalen > t->slot_size - RPMI_MSG_HDR_SIZE ||
	    hdr->datalen > max)
		rc = RPMI_ERR_BAD_RANGE;
	else
		for (unsigned int i = 0; i < hdr->datalen / U(4); i++)
			words[i] = io_read32(slot + 8 + 4 * i);

	io_write32(queue_slot(t, q, 0), (head + 1) % t->msg_slots[q]);
	return rc;
}

static int rpmi_shmem_probe(const void *fdt, int node)
{
	struct rpmi_shmem *t = &instances[nr_instances];
	uint64_t base = 0, size = 0;
	uint32_t slot = 0;

	if (node < 0)
		return 0;
	if (nr_instances == CONFIG_RPMI_SHMEM_MAX_INSTANCES)
		return -1;

	/* A power of two, at least 64 bytes. */
	slot = fdt_prop_u32(fdt, node, "riscv,slot-size", RPMI_SLOT_SIZE_MIN);
	if (slot < RPMI_SLOT_SIZE_MIN || !IS_POWER_OF_TWO(slot))
		return -1;

	*t = (struct rpmi_shmem){ .slot_size = slot };
	for (unsigned int q = 0; q < RPMI_QUEUE_COUNT; q++) {
		if (fdt_reg_by_name(fdt, node, queue_names[q], &base, &size))
			continue;
		/* Head and tail slots, and at least one for a message. */
		if (size % slot || size / slot < 3)
			return -1;
		t->queue[q] = (uintptr_t)base;
		t->msg_slots[q] = (uint32_t)(size / slot) - 2;
		/*
		 * The PuC's transport is the monitor's alone, unless the
		 * platform says S-mode has business there (a test model).
		 */
		memregion_add((unsigned long)base, (unsigned long)size,
			      IS_ENABLED_RPMI_SHMEM_PROTECT ?
			      MEMREGION_MMODE_RW :
			      MEMREGION_SHARED_RW);
	}
	if (!t->queue[RPMI_QUEUE_A2P_REQ] || !t->queue[RPMI_QUEUE_P2A_ACK])
		return -1;
	if (!fdt_reg_by_name(fdt, node, "a2p-doorbell", &base, &size)) {
		t->doorbell = (vaddr_t)base;
		t->doorbell_value =
			fdt_prop_u32(fdt, node, "riscv,a2p-doorbell-value", 1);
		memregion_add((unsigned long)base, (unsigned long)size,
			      MEMREGION_MMODE_RW);
	}

	t->ctx.name = "rpmi-shmem";
	t->ctx.phandle = fdt_get_phandle(fdt, node);
	t->ctx.max_data_len = slot - RPMI_MSG_HDR_SIZE;
	t->ctx.has_p2a = t->queue[RPMI_QUEUE_P2A_REQ] &&
			 t->queue[RPMI_QUEUE_A2P_ACK];
	t->ctx.p2a_doorbell_sysmsi =
		fdt_prop_u32(fdt, node, "riscv,p2a-doorbell-sysmsi-index",
			     RPMI_NO_SYSMSI);
	t->ctx.priv = t;
	t->ctx.send = rpmi_shmem_send;
	t->ctx.recv = rpmi_shmem_recv;
	rpmi_context_register(&t->ctx);
	nr_instances++;

	pr_info("rpmi-shmem: %lx, %u-byte slots, %u + %u messages%s\n",
		(unsigned long)t->queue[RPMI_QUEUE_A2P_REQ], slot,
		t->msg_slots[RPMI_QUEUE_A2P_REQ],
		t->msg_slots[RPMI_QUEUE_P2A_REQ],
		t->doorbell ? ", doorbell" : "");
	return 0;
}

static const char *const rpmi_shmem_compatible[] = { "riscv,rpmi-shmem-mbox",
						     NULL };

DRIVER_DEFINE(rpmi_shmem) = {
	.name = "rpmi-shmem",
	.compatible = rpmi_shmem_compatible,
	.stage = DRIVER_STAGE_EARLY,
	.mmode_only = true,
	.probe = rpmi_shmem_probe,
};
