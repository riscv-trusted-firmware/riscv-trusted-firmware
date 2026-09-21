// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Remote fences. Every hart has a short queue of requests to serve. A
 * requester puts a copy of its request into the queue of each target,
 * interrupts them, does its own part and waits until all of them are done
 * with theirs: each entry points at the requester's count of requests still
 * out. Harts that fence at the same time only meet at the queues they
 * have in common, so nothing here is serialised system-wide.
 *
 * Whoever waits (for room in a queue, for its targets) keeps serving its
 * own queue, so two harts fencing each other cannot deadlock. A queue is
 * served from the IPI and, for a hart that stops, once more on the way
 * down: a target is picked among the harts that run, and may have stopped
 * running by the time the request arrives.
 */

#include <arch/hart.h>
#include <arch/pmu.h>
#include <arch/rfence.h>
#include <atomic.h>
#include <heap.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <util.h>

#define PAGE_SIZE UL(4096)
/* Beyond this many pages a full flush is cheaper than a walk. */
#define RFENCE_MAX_PAGES UL(64)

#define RFENCE_QUEUE_LEN 8

struct rfence_queue {
	unsigned long lock; /* the queue and its counts */
	unsigned int head, count;
	struct {
		struct rfence_req req;
		unsigned long *out; /* the requester's count, atomic */
	} entries[RFENCE_QUEUE_LEN];
};

/* One per hart of the hart table. */
static struct rfence_queue *queues;

void rfence_init(void)
{
	queues = heap_alloc_array(hart_table_size(), sizeof(*queues));
}

/* hfence.* by encoding: assemblers want the H extension in -march. */
static inline void hfence_vvma(unsigned long addr, unsigned long asid)
{
	__asm__ __volatile__(".insn r 0x73, 0, 0x11, x0, %0, %1"
			     :
			     : "r"(addr), "r"(asid)
			     : "memory");
}

static inline void hfence_gvma(unsigned long addr, unsigned long vmid)
{
	__asm__ __volatile__(".insn r 0x73, 0, 0x31, x0, %0, %1"
			     :
			     : "r"(addr), "r"(vmid)
			     : "memory");
}

static inline void sfence_vma(unsigned long addr, unsigned long asid)
{
	__asm__ __volatile__("sfence.vma %0, %1"
			     :
			     : "r"(addr), "r"(asid)
			     : "memory");
}

/*
 * rs1/rs2 = x0 mean "all addresses" / "all address spaces". A non-zero
 * register holding 0 is ASID/VMID 0, so the wildcard forms are spelled out.
 */
static void fence_one(const struct rfence_req *req, unsigned long addr)
{
	register unsigned long a __asm__("a0") = addr;
	register unsigned long id __asm__("a1") = req->asid;

	switch (req->type) {
	case RFENCE_SFENCE_VMA:
		__asm__ __volatile__("sfence.vma a0, x0" : : "r"(a) : "memory");
		break;
	case RFENCE_SFENCE_VMA_ASID:
		sfence_vma(a, id);
		break;
	case RFENCE_HFENCE_GVMA:
		/* Guest physical addresses are passed shifted right by 2. */
		a >>= 2;
		__asm__ __volatile__(".insn r 0x73, 0, 0x31, x0, a0, x0"
				     :
				     : "r"(a)
				     : "memory");
		break;
	case RFENCE_HFENCE_GVMA_VMID:
		a >>= 2;
		hfence_gvma(a, id);
		break;
	case RFENCE_HFENCE_VVMA:
		__asm__ __volatile__(".insn r 0x73, 0, 0x11, x0, a0, x0"
				     :
				     : "r"(a)
				     : "memory");
		break;
	case RFENCE_HFENCE_VVMA_ASID:
		hfence_vvma(a, id);
		break;
	default:
		break;
	}
}

static void fence_all(const struct rfence_req *req)
{
	register unsigned long id __asm__("a1") = req->asid;

	switch (req->type) {
	case RFENCE_SFENCE_VMA:
		__asm__ __volatile__("sfence.vma x0, x0" ::: "memory");
		break;
	case RFENCE_SFENCE_VMA_ASID:
		__asm__ __volatile__("sfence.vma x0, a1"
				     :
				     : "r"(id)
				     : "memory");
		break;
	case RFENCE_HFENCE_GVMA:
		__asm__ __volatile__(".insn r 0x73, 0, 0x31, x0, x0, x0" ::
				     : "memory");
		break;
	case RFENCE_HFENCE_GVMA_VMID:
		__asm__ __volatile__(".insn r 0x73, 0, 0x31, x0, x0, a1"
				     :
				     : "r"(id)
				     : "memory");
		break;
	case RFENCE_HFENCE_VVMA:
		__asm__ __volatile__(".insn r 0x73, 0, 0x11, x0, x0, x0" ::
				     : "memory");
		break;
	case RFENCE_HFENCE_VVMA_ASID:
		__asm__ __volatile__(".insn r 0x73, 0, 0x11, x0, x0, a1"
				     :
				     : "r"(id)
				     : "memory");
		break;
	default:
		break;
	}
}

static void fence_local(const struct rfence_req *req)
{
	if (req->type == RFENCE_FENCE_I) {
		__asm__ __volatile__("fence.i" ::: "memory");
		return;
	}

	if ((req->start == 0 && req->size == 0) || req->size == ~UL(0) ||
	    req->size > RFENCE_MAX_PAGES * PAGE_SIZE) {
		fence_all(req);
		return;
	}

	for (unsigned long off = 0; off < req->size; off += PAGE_SIZE)
		fence_one(req, req->start + off);
}

void rfence_process(void)
{
	struct rfence_queue *q = &queues[this_hart_index()];

	for (;;) {
		struct rfence_req req = {};
		unsigned long *out = NULL;

		spin_lock(&q->lock);
		if (!q->count) {
			spin_unlock(&q->lock);
			return;
		}
		req = q->entries[q->head].req;
		out = q->entries[q->head].out;
		q->head = (q->head + 1) % RFENCE_QUEUE_LEN;
		q->count--;
		spin_unlock(&q->lock);

		/* Sent/received event pairs follow enum rfence_type. */
		pmu_fw_event(SBI_PMU_FW_FENCE_I_RECEIVED + 2 * req.type);
		fence_local(&req);
		/* The requester's stack: not to be touched after this. */
		atomic_add_ulong(out, -UL(1));
	}
}

/* false: the target's queue is full. */
static bool enqueue(unsigned int target, const struct rfence_req *req,
		    unsigned long *out)
{
	struct rfence_queue *q = &queues[target];
	bool room = false;

	spin_lock(&q->lock);
	room = q->count < RFENCE_QUEUE_LEN;
	if (room) {
		unsigned int tail = (q->head + q->count++) % RFENCE_QUEUE_LEN;

		q->entries[tail].req = *req;
		q->entries[tail].out = out;
		atomic_add_ulong(out, 1);
	}
	spin_unlock(&q->lock);
	return room;
}

int rfence_request(const struct hartmask *targets, const struct rfence_req *req)
{
	unsigned int self = this_hart_index();
	unsigned long out = 0;
	unsigned int index = 0;

	if (req->type >= RFENCE_HFENCE_GVMA && !hart_has(HART_FEAT_H))
		return SBI_ERR_NOT_SUPPORTED;

	for_each_hart_in_mask(index, targets) {
		if (index == self)
			continue;
		/*
		 * A full queue drains as its hart gets to it: that may be
		 * waiting for us.
		 */
		while (!enqueue(index, req, &out)) {
			ipi_send(index, IPI_EVENT_RFENCE);
			ipi_process();
		}
		pmu_fw_event(SBI_PMU_FW_FENCE_I_SENT + 2 * req->type);
		ipi_send(index, IPI_EVENT_RFENCE);
	}

	if (hartmask_test(targets, self))
		fence_local(req);

	/*
	 * Its own queue meanwhile, and the word to stop if the monitor has
	 * panicked.
	 */
	while (atomic_load_ulong(&out)) {
		rfence_process();
		ipi_process();
		cpu_relax();
	}
	return SBI_SUCCESS;
}
