// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Remote fences. One request is in flight at a time: the requester takes
 * the lock, publishes the request, interrupts the targets and waits until
 * each has cleared its bit in the pending mask. A hart waiting for the
 * lock keeps serving requests aimed at it, so two harts fencing each other
 * cannot deadlock.
 */

#include <arch/hart.h>
#include <arch/rfence.h>
#include <atomic.h>
#include <ipi.h>
#include <sbi/sbi.h>
#include <spinlock.h>
#include <util.h>

#define PAGE_SIZE UL(4096)
/* Beyond this many pages a full flush is cheaper than a walk. */
#define RFENCE_MAX_PAGES UL(64)

static unsigned long rfence_lock = SPINLOCK_UNLOCK;
static struct rfence_req rfence_cur;
static struct hartmask rfence_pending;

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
	unsigned long self = this_hartid();

	if (!hartmask_test(&rfence_pending, self))
		return;
	fence_local(&rfence_cur);
	hartmask_clear_atomic(&rfence_pending, self);
}

int rfence_request(const struct hartmask *targets, const struct rfence_req *req)
{
	unsigned long self = this_hartid();
	unsigned long hartid = 0;
	bool local = hartmask_test(targets, self);

	if (req->type >= RFENCE_HFENCE_GVMA && !hart_has(HART_FEAT_H))
		return SBI_ERR_NOT_SUPPORTED;

	while (!spin_trylock(&rfence_lock))
		ipi_process();

	rfence_cur = *req;
	for_each_hart_in_mask(hartid, targets) {
		if (hartid == self)
			continue;
		atomic_or_ulong(&rfence_pending.bits[hartid / BITS_PER_LONG],
				BIT(hartid % BITS_PER_LONG));
	}
	for_each_hart_in_mask(hartid, targets)
		if (hartid != self)
			ipi_send(hartid, IPI_EVENT_RFENCE);

	if (local)
		fence_local(req);

	while (!hartmask_empty_atomic(&rfence_pending))
		cpu_relax();

	spin_unlock(&rfence_lock);
	return SBI_SUCCESS;
}
