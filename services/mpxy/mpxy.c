// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * MPXY core: per-hart shared memory, channel list, standard attributes.
 *
 * The shared memory is S-mode's: another hart can change it while a call
 * is in progress. Everything is therefore read from it once, and nothing
 * read back from it is trusted.
 */

#include <arch/hart.h>
#include <arch/pmp.h>
#include <arch/sse.h>
#include <atomic.h>
#include <domain.h>
#include <fdt_util.h>
#include <mpxy.h>
#include <sbi/sbi.h>
#include <string.h>
#include <util.h>

#define SHMEM_NONE MPXY_SHMEM_NONE

static struct mpxy_channel *channels;
static unsigned int nr_channels;
/* All zero at boot is a valid address: mpxy_hart_init() runs before S-mode. */
static unsigned long shmem[CONFIG_PLATFORM_HART_COUNT];

void mpxy_hart_init(void)
{
	shmem[this_hart_index()] = SHMEM_NONE;
}

unsigned long mpxy_hart_shmem_swap(unsigned long addr)
{
	unsigned long old = shmem[this_hart_index()];

	shmem[this_hart_index()] = addr;
	return old;
}

static unsigned int nr_sse_events;

long mpxy_channel_register(struct mpxy_channel *ch)
{
	struct mpxy_channel **p = &channels;

	/* Sorted by id: get_channel_ids then has a stable order. */
	for (; *p && (*p)->id <= ch->id; p = &(*p)->next)
		if ((*p)->id == ch->id)
			return SBI_ERR_ALREADY_AVAILABLE;
	/*
	 * Events can be signalled: by an MSI always, by SSE while events last.
	 */
	if (ch->capability & MPXY_CAP_GET_NOTIFICATIONS) {
		ch->capability |= MPXY_CAP_MSI;
		if (nr_sse_events < SSE_MPXY_EVENTS) {
			ch->sse_event_id =
				(uint32_t)SSE_EVENT_MPXY(nr_sse_events++);
			ch->capability |= MPXY_CAP_SSE;
		}
	}
	ch->next = *p;
	*p = ch;
	nr_channels++;
	return SBI_SUCCESS;
}

/* Set when any channel's events_due is: the common case costs one load. */
static unsigned long indications_due;

void mpxy_channel_events_due(struct mpxy_channel *ch)
{
	if (!(ch->capability & (MPXY_CAP_MSI | MPXY_CAP_SSE)))
		return;
	atomic_store_ulong(&ch->events_due, 1);
	atomic_store_ulong(&indications_due, 1);
}

void mpxy_indicate(void)
{
	if (!atomic_load_ulong(&indications_due) ||
	    !atomic_swap_ulong(&indications_due, 0))
		return;
	for (struct mpxy_channel *ch = channels; ch; ch = ch->next) {
		if (!atomic_swap_ulong(&ch->events_due, 0))
			continue;
		/* The MSI if there is one, as the specification prefers. */
		if (ch->msi_control) {
			uint64_t addr = reg_pair_to_64(ch->msi_addr_high,
						       ch->msi_addr_low);

			smode_poke32((unsigned long)addr, ch->msi_data);
		} else if (ch->capability & MPXY_CAP_SSE) {
			/*
			 * Whoever has the event registered: it is per domain.
			 */
			for (unsigned int key = 0; key < domain_keys(); key++)
				if (ch->owner == MPXY_OWNER_ANY ||
				    ch->owner == key + 1)
					sse_raise_global(ch->sse_event_id, key);
		}
	}
}

unsigned int mpxy_channel_count(void)
{
	return nr_channels;
}

/* Is the channel the calling domain's to see? */
static bool channel_visible(const struct mpxy_channel *ch)
{
	return ch->owner == MPXY_OWNER_ANY ||
	       ch->owner == this_domain_key() + 1;
}

static struct mpxy_channel *channel_find(unsigned long id)
{
	for (struct mpxy_channel *ch = channels; ch; ch = ch->next)
		if (ch->id == id)
			return channel_visible(ch) ? ch : NULL;
	return NULL;
}

int mpxy_channel_owner_from_fdt(const void *fdt, int node, unsigned int *owner)
{
	*owner = MPXY_OWNER_ANY;
#ifdef CONFIG_DOMAINS
	uint32_t phandle = fdt_prop_u32(fdt, node, "riscv,domain", 0);

	if (phandle) {
		const struct domain *dom = domain_by_phandle(phandle);

		/* Not the wrong domain's for a typing mistake in the tree. */
		if (!dom)
			return -1;
		*owner = dom->index + 1;
	}
#endif
	return 0;
}

static uint32_t *this_shmem(void)
{
	unsigned long addr = shmem[this_hart_index()];

	return addr == SHMEM_NONE ? NULL : (uint32_t *)addr;
}

uint32_t *mpxy_hart_shmem(void)
{
	return this_shmem();
}

void mpxy_shmem_access(bool begin)
{
	unsigned long addr = shmem[this_hart_index()];

	if (addr == SHMEM_NONE)
		return;
	if (begin)
		smode_access_begin(addr, MPXY_SHMEM_SIZE);
	else
		smode_access_end();
}

long mpxy_set_shmem(unsigned long lo, unsigned long hi, unsigned long flags)
{
	unsigned long *cur = &shmem[this_hart_index()], old = *cur;
	unsigned long *new = NULL;

	if (flags > 1)
		return SBI_ERR_INVALID_PARAM;
	if (lo == ~UL(0) && hi == ~UL(0)) {
		*cur = SHMEM_NONE;
		return SBI_SUCCESS;
	}
	if (!IS_ALIGNED(lo, MPXY_SHMEM_SIZE))
		return SBI_ERR_INVALID_PARAM;
	/* No physical address above XLEN bits is reachable from M-mode. */
	if (hi || !smode_range_ok(lo, MPXY_SHMEM_SIZE))
		return SBI_ERR_INVALID_ADDRESS;

	*cur = lo;
	if (flags & 1) {
		/*
		 * OVERWRITE-RETURN: the previous (lo, hi), all-ones when none.
		 */
		new = smode_access_begin(lo, 2 * sizeof(*new));
		new[0] = old;
		new[1] = old == SHMEM_NONE ? ~UL(0) : 0;
		smode_access_end();
	}
	return SBI_SUCCESS;
}

long mpxy_get_channel_ids(unsigned long start_index)
{
	uint32_t *mem = this_shmem();
	unsigned long max = MPXY_SHMEM_SIZE / 4 - 2, n = 0, i = 0, visible = 0;

	if (!mem)
		return SBI_ERR_NO_SHMEM;
	/* The list is the caller's domain's. */
	for (struct mpxy_channel *ch = channels; ch; ch = ch->next)
		visible += channel_visible(ch);
	if (start_index > visible || (start_index && start_index == visible))
		return SBI_ERR_INVALID_PARAM;

	for (struct mpxy_channel *ch = channels; ch; ch = ch->next) {
		if (!channel_visible(ch))
			continue;
		if (i++ >= start_index && n < max)
			mem[2 + n++] = ch->id;
	}
	mem[0] = (uint32_t)(visible - start_index - n);
	mem[1] = (uint32_t)n;
	return SBI_SUCCESS;
}

static uint32_t std_attr_read(const struct mpxy_channel *ch, uint32_t id)
{
	switch (id) {
	case MPXY_ATTR_MSG_PROT_ID:
		return ch->msg_prot_id;
	case MPXY_ATTR_MSG_PROT_VERSION:
		return ch->msg_prot_version;
	case MPXY_ATTR_MSG_DATA_MAX_LEN:
		return ch->msg_data_max_len;
	case MPXY_ATTR_MSG_SEND_TIMEOUT:
		return ch->send_timeout_us;
	case MPXY_ATTR_MSG_COMPLETION_TIMEOUT:
		return ch->completion_timeout_us;
	case MPXY_ATTR_CHANNEL_CAPABILITY:
		return ch->capability;
	case MPXY_ATTR_EVENTS_STATE_CONTROL:
		return ch->events_state_control;
	case MPXY_ATTR_SSE_EVENT_ID:
		return ch->sse_event_id;
	case MPXY_ATTR_MSI_CONTROL:
		return ch->msi_control;
	case MPXY_ATTR_MSI_ADDR_LOW:
		return ch->msi_addr_low;
	case MPXY_ATTR_MSI_ADDR_HIGH:
		return ch->msi_addr_high;
	case MPXY_ATTR_MSI_DATA:
		return ch->msi_data;
	default:
		return 0;
	}
}

/* A write goes to a copy of the channel: a bad one changes nothing. */
static long std_attr_write(struct mpxy_channel *ch, uint32_t id, uint32_t val)
{
	/* Writes are ignored without the capability. */
	bool msi = ch->capability & MPXY_CAP_MSI;

	switch (id) {
	case MPXY_ATTR_MSI_CONTROL:
		if (val > 1)
			return SBI_ERR_INVALID_PARAM;
		ch->msi_control = msi ? val : 0;
		return SBI_SUCCESS;
	case MPXY_ATTR_MSI_ADDR_LOW:
		ch->msi_addr_low = msi ? val : 0;
		return SBI_SUCCESS;
	case MPXY_ATTR_MSI_ADDR_HIGH:
		ch->msi_addr_high = msi ? val : 0;
		return SBI_SUCCESS;
	case MPXY_ATTR_MSI_DATA:
		ch->msi_data = msi ? val : 0;
		return SBI_SUCCESS;
	case MPXY_ATTR_EVENTS_STATE_CONTROL:
		if (val > 1)
			return SBI_ERR_INVALID_PARAM;
		if (ch->capability & MPXY_CAP_EVENTS_STATE)
			ch->events_state_control = val;
		return SBI_SUCCESS;
	default:
		/* Read-only. */
		return SBI_ERR_BAD_RANGE;
	}
}

/*
 * An MSI that is enabled is a write of the monitor's to an address of
 * S-mode's choosing: a word the caller's domain could write itself, which
 * its interrupt files are and the monitor's memory is not.
 */
static bool msi_target_ok(const struct mpxy_channel *ch)
{
	uint64_t addr = reg_pair_to_64(ch->msi_addr_high, ch->msi_addr_low);

	return !ch->msi_control ||
	       (IS_ALIGNED(addr, 4) && addr == (unsigned long)addr &&
		smode_range_ok((unsigned long)addr, 4));
}

/* Common checks of the attribute calls; *prot: message protocol range. */
static long attr_range_check(unsigned long channel_id, unsigned long base,
			     unsigned long count, struct mpxy_channel **ch,
			     bool *prot)
{
	*ch = channel_id > 0xffffffffUL ? NULL : channel_find(channel_id);
	if (!*ch)
		return SBI_ERR_NOT_SUPPORTED;
	if (!count || count > MPXY_SHMEM_SIZE / 4 || base > UL(0xffffffff))
		return SBI_ERR_INVALID_PARAM;

	*prot = base >= MPXY_ATTR_MSG_PROT_START;
	if (*prot ? count > ULL(0x100000000) - base :
	    base + count > MPXY_ATTR_STD_COUNT)
		return SBI_ERR_BAD_RANGE;
	return SBI_SUCCESS;
}

long mpxy_read_attributes(unsigned long channel_id, unsigned long base,
			  unsigned long count)
{
	uint32_t *mem = this_shmem();
	struct mpxy_channel *ch = NULL;
	bool prot = false;
	long rc = 0;

	if (!mem)
		return SBI_ERR_NO_SHMEM;
	rc = attr_range_check(channel_id, base, count, &ch, &prot);
	if (rc)
		return rc;

	for (unsigned long i = 0; i < count; i++) {
		uint32_t id = (uint32_t)(base + i), val = 0;

		if (!prot) {
			val = std_attr_read(ch, id);
		} else {
			if (!ch->ops->read_attr)
				return SBI_ERR_BAD_RANGE;
			rc = ch->ops->read_attr(ch, id, &val);
			if (rc)
				return rc;
		}
		mem[i] = val;
	}
	return SBI_SUCCESS;
}

long mpxy_write_attributes(unsigned long channel_id, unsigned long base,
			   unsigned long count)
{
	uint32_t *mem = this_shmem();
	struct mpxy_channel *ch = NULL, staged = {};
	bool prot = false;
	long rc = 0;

	if (!mem)
		return SBI_ERR_NO_SHMEM;
	rc = attr_range_check(channel_id, base, count, &ch, &prot);
	if (rc)
		return rc;

	if (prot) {
		/* The protocol sees the values one by one, read once each. */
		for (unsigned long i = 0; i < count; i++) {
			if (!ch->ops->write_attr)
				return SBI_ERR_BAD_RANGE;
			rc = ch->ops->write_attr(ch, (uint32_t)(base + i),
						 mem[i]);
			if (rc)
				return rc;
		}
		return SBI_SUCCESS;
	}

	/*
	 * Standard attributes: all of them valid, and as a whole, or none
	 * written.
	 */
	staged = *ch;
	for (unsigned long i = 0; i < count; i++) {
		rc = std_attr_write(&staged, (uint32_t)(base + i), mem[i]);
		if (rc)
			return rc;
	}
	if (!msi_target_ok(&staged))
		return SBI_ERR_INVALID_PARAM;
	ch->msi_control = 0;
	ch->msi_addr_low = staged.msi_addr_low;
	ch->msi_addr_high = staged.msi_addr_high;
	ch->msi_data = staged.msi_data;
	ch->msi_domain = this_domain_key();
	ch->msi_control = staged.msi_control;
	ch->events_state_control = staged.events_state_control;
	return SBI_SUCCESS;
}

long mpxy_send_message(unsigned long channel_id, unsigned long msg_id,
		       unsigned long len, unsigned long *resp_len)
{
	uint32_t *mem = this_shmem();
	struct mpxy_channel *ch = NULL;
	uint32_t want = resp_len ? MPXY_CAP_SEND_WITH_RESP :
				   MPXY_CAP_SEND_WITHOUT_RESP;

	if (!mem)
		return SBI_ERR_NO_SHMEM;
	ch = channel_id > UL(0xffffffff) ? NULL : channel_find(channel_id);
	if (!ch || !(ch->capability & want) || msg_id > UL(0xffffffff))
		return SBI_ERR_NOT_SUPPORTED;
	if (len > ch->msg_data_max_len || len > MPXY_SHMEM_SIZE)
		return SBI_ERR_INVALID_PARAM;

	return ch->ops->send(ch, (uint32_t)msg_id, mem, len, MPXY_SHMEM_SIZE,
			     resp_len);
}

long mpxy_get_notifications(unsigned long channel_id, unsigned long *bytes)
{
	/*
	 * Events state (REMAINING, RETURNED, LOST, reserved), then the events.
	 */
	const unsigned long hdr = 16;
	uint32_t *mem = this_shmem();
	struct mpxy_channel *ch = NULL;
	struct mpxy_events ev = { 0 };
	long rc = 0;

	if (!mem)
		return SBI_ERR_NO_SHMEM;
	ch = channel_id > UL(0xffffffff) ? NULL : channel_find(channel_id);
	if (!ch || !(ch->capability & MPXY_CAP_GET_NOTIFICATIONS))
		return SBI_ERR_NOT_SUPPORTED;

	rc = ch->ops->get_events(ch, (char *)mem + hdr, MPXY_SHMEM_SIZE - hdr,
				 &ev);
	if (rc)
		return rc;
	if (ch->events_state_control) {
		mem[0] = ev.remaining;
		mem[1] = ev.returned;
		mem[2] = ev.lost;
		mem[3] = 0;
	}
	*bytes = ev.bytes;
	return SBI_SUCCESS;
}
