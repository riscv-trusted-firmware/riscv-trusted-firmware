// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * SBI MPXY tests. The channels lead through the monitor's RPMI client and
 * its shared memory transport to the PuC model (puc.c), which another hart
 * is serving while these run.
 */

#include <io.h>
#include <mpxy.h>
#include <rpmi.h>
#include <util.h>

#include "sbicall.h"
#include "sbitest.h"

#ifdef CONFIG_QEMU_VIRT_RPMI

#define FID_GET_SHMEM_SIZE 0
#define FID_SET_SHMEM 1
#define FID_GET_CHANNEL_IDS 2
#define FID_READ_ATTRS 3
#define FID_WRITE_ATTRS 4
#define FID_SEND_WITH_RESP 5
#define FID_SEND_WITHOUT_RESP 6
#define FID_GET_NOTIFICATIONS 7

#define CH_CLOCK CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE
#define CH_VOLTAGE (CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE + 1)
#define CH_BOGUS UL(0xdead)

#define MAX_DATA (CONFIG_QEMU_VIRT_RPMI_SLOT_SIZE - RPMI_MSG_HDR_SIZE)
#define RPMI_ATTR_BASE UL(0x80000000)

#define CLOCK_GET_NUM_CLOCKS 0x02
#define CLOCK_SET_RATE 0x06
#define CLOCK_GET_RATE 0x07

/* Two shared memory pages; as 32-bit words and as XLEN words. */
static union {
	uint32_t w[MPXY_SHMEM_SIZE / 4];
	unsigned long l[2];
} __aligned(4096) page_a, page_b;

static struct sbiret set_shmem(void *page, unsigned long flags)
{
	return sbi_call3(SBI_EXT_MPXY, FID_SET_SHMEM, (unsigned long)page, 0,
			 flags);
}

static struct sbiret send(unsigned long ch, unsigned long service,
			  unsigned long len)
{
	return sbi_call3(SBI_EXT_MPXY, FID_SEND_WITH_RESP, ch, service, len);
}

static void test_shmem(void)
{
	struct sbiret ret = {};

	ret = sbi_call0(SBI_EXT_MPXY, FID_GET_SHMEM_SIZE);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == (long)MPXY_SHMEM_SIZE, "shared memory size %ld",
	      ret.value);

	/* Nothing works before the shared memory is set. */
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 0),
		  SBI_ERR_NO_SHMEM);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 0, 1),
		  SBI_ERR_NO_SHMEM);
	CHECK_RET(send(CH_CLOCK, CLOCK_GET_NUM_CLOCKS, 0), SBI_ERR_NO_SHMEM);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_NOTIFICATIONS, CH_CLOCK),
		  SBI_ERR_NO_SHMEM);

	CHECK_RET(set_shmem((char *)&page_a + 64, 0), SBI_ERR_INVALID_PARAM);
	CHECK_RET(set_shmem(&page_a, 2), SBI_ERR_INVALID_PARAM);
	CHECK_RET(set_shmem((void *)monitor_addr, 0), SBI_ERR_INVALID_ADDRESS);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_SET_SHMEM, (unsigned long)&page_a,
			    1, 0),
		  SBI_ERR_INVALID_ADDRESS);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 0),
		  SBI_ERR_NO_SHMEM);

	/*
	 * OVERWRITE-RETURN hands back what was set before: nothing, then page
	 * A.
	 */
	CHECK_RET(set_shmem(&page_a, 1), SBI_SUCCESS);
	CHECK(page_a.l[0] == ~UL(0) && page_a.l[1] == ~UL(0),
	      "previous shared memory %lx %lx, expected none", page_a.l[0],
	      page_a.l[1]);
	CHECK_RET(set_shmem(&page_b, 1), SBI_SUCCESS);
	CHECK(page_b.l[0] == (unsigned long)&page_a && page_b.l[1] == 0,
	      "previous shared memory %lx %lx", page_b.l[0], page_b.l[1]);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_SET_SHMEM, ~UL(0), ~UL(0), 0),
		  SBI_SUCCESS);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 0),
		  SBI_ERR_NO_SHMEM);
	CHECK_RET(set_shmem(&page_a, 0), SBI_SUCCESS);
}

static void test_channels(void)
{
	struct sbiret ret = {};

	ret = sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(page_a.w[0] == 0 && page_a.w[1] == 2, "remaining %u returned %u",
	      page_a.w[0], page_a.w[1]);
	CHECK(page_a.w[2] == CH_CLOCK && page_a.w[3] == CH_VOLTAGE,
	      "channel ids %x %x", page_a.w[2], page_a.w[3]);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 1), SBI_SUCCESS);
	CHECK(page_a.w[0] == 0 && page_a.w[1] == 1 && page_a.w[2] == CH_VOLTAGE,
	      "from index 1: %u %u %x", page_a.w[0], page_a.w[1], page_a.w[2]);
	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_CHANNEL_IDS, 2),
		  SBI_ERR_INVALID_PARAM);
}

static void test_attributes(void)
{
	struct sbiret ret = {};

	ret = sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 0,
			MPXY_ATTR_STD_COUNT);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(page_a.w[MPXY_ATTR_MSG_PROT_ID] == MPXY_MSG_PROT_RPMI,
	      "protocol %x", page_a.w[MPXY_ATTR_MSG_PROT_ID]);
	CHECK(page_a.w[MPXY_ATTR_MSG_DATA_MAX_LEN] == MAX_DATA,
	      "max data length %u", page_a.w[MPXY_ATTR_MSG_DATA_MAX_LEN]);
	CHECK(page_a.w[MPXY_ATTR_MSG_SEND_TIMEOUT] &&
	      page_a.w[MPXY_ATTR_MSG_COMPLETION_TIMEOUT] >=
			      page_a.w[MPXY_ATTR_MSG_SEND_TIMEOUT],
	      "timeouts");
	CHECK(page_a.w[MPXY_ATTR_CHANNEL_CAPABILITY] ==
	      (MPXY_CAP_SEND_WITH_RESP | MPXY_CAP_SEND_WITHOUT_RESP |
	       MPXY_CAP_GET_NOTIFICATIONS | MPXY_CAP_EVENTS_STATE),
	      "capability %x", page_a.w[MPXY_ATTR_CHANNEL_CAPABILITY]);
	CHECK(page_a.w[MPXY_ATTR_EVENTS_STATE_CONTROL] == 0,
	      "events state on at reset");

	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 0,
			    MPXY_ATTR_STD_COUNT + 1),
		  SBI_ERR_BAD_RANGE);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 0, 0),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 0,
			    MPXY_SHMEM_SIZE / 4 + 1),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_BOGUS, 0, 1),
		  SBI_ERR_NOT_SUPPORTED);

	/* RPMI attributes: what the PuC says about itself and the group. */
	ret = sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, RPMI_ATTR_BASE,
			4);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(page_a.w[0] == RPMI_GROUP_CLOCK &&
	      page_a.w[1] == RPMI_VERSION(1, 0),
	      "service group %x version %x", page_a.w[0], page_a.w[1]);
	CHECK(page_a.w[2] == PUC_IMPL_ID && page_a.w[3] == PUC_IMPL_VERSION,
	      "implementation %x version %x", page_a.w[2], page_a.w[3]);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK,
			    RPMI_ATTR_BASE, 5),
		  SBI_ERR_BAD_RANGE);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK, 1, 1),
		  SBI_SUCCESS);
	CHECK(page_a.w[0] == RPMI_VERSION(1, 0), "RPMI version %x",
	      page_a.w[0]);

	/* The voltage group is published but the PuC does not implement it. */
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_VOLTAGE,
			    RPMI_ATTR_BASE, 1),
		  SBI_SUCCESS);
	CHECK(page_a.w[0] == RPMI_GROUP_VOLTAGE, "service group %x",
	      page_a.w[0]);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_VOLTAGE,
			    RPMI_ATTR_BASE, 2),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(send(CH_VOLTAGE, 0x02, 0), SBI_ERR_NOT_SUPPORTED);

	/*
	 * Writes: events state is the one standard attribute that takes them.
	 */
	page_a.w[0] = 1;
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_WRITE_ATTRS, CH_CLOCK,
			    MPXY_ATTR_EVENTS_STATE_CONTROL, 1),
		  SBI_SUCCESS);
	page_a.w[0] = 0x1234; /* MSI data: ignored without MSI support */
	/* not a valid events state: nothing is written */
	page_a.w[1] = 7;
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_WRITE_ATTRS, CH_CLOCK,
			    MPXY_ATTR_MSI_DATA, 2),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_READ_ATTRS, CH_CLOCK,
			    MPXY_ATTR_MSI_DATA, 2),
		  SBI_SUCCESS);
	CHECK(page_a.w[0] == 0 && page_a.w[1] == 1,
	      "MSI data %x, events state %u", page_a.w[0], page_a.w[1]);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_WRITE_ATTRS, CH_CLOCK,
			    MPXY_ATTR_MSG_PROT_ID, 1),
		  SBI_ERR_BAD_RANGE);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_WRITE_ATTRS, CH_CLOCK,
			    RPMI_ATTR_BASE, 1),
		  SBI_ERR_BAD_RANGE);
}

static void test_messages(void)
{
	struct sbiret ret = {};
	uint64_t rate = 0;

	ret = send(CH_CLOCK, CLOCK_GET_NUM_CLOCKS, 0);
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 8 && page_a.w[0] == 0 &&
	      page_a.w[1] == PUC_NUM_CLOCKS,
	      "num_clocks: %ld bytes, status %d, %u clocks", ret.value,
	      (int)page_a.w[0], page_a.w[1]);

	page_a.w[0] = 1;
	ret = send(CH_CLOCK, CLOCK_GET_RATE, 4);
	CHECK_RET(ret, SBI_SUCCESS);
	rate = reg_pair_to_64(page_a.w[2], page_a.w[1]);
	CHECK(ret.value == 12 && page_a.w[0] == 0 && rate == puc_clock_rate(1),
	      "get_rate: %ld bytes, status %d, rate %llx", ret.value,
	      (int)page_a.w[0], (unsigned long long)rate);

	page_a.w[0] = 1;
	page_a.w[1] = 0;
	page_a.w[2] = 0x89abcdef;
	page_a.w[3] = 0x01234567;
	ret = send(CH_CLOCK, CLOCK_SET_RATE, 16);
	CHECK(ret.error == 0 && ret.value == 4 && page_a.w[0] == 0, "set_rate");
	page_a.w[0] = 1;
	send(CH_CLOCK, CLOCK_GET_RATE, 4);
	CHECK(page_a.w[1] == 0x89abcdef && page_a.w[2] == 0x01234567,
	      "rate read back %x %x", page_a.w[2], page_a.w[1]);

	/* The service's verdict travels in the STATUS word, not in sbiret. */
	page_a.w[0] = PUC_NUM_CLOCKS;
	ret = send(CH_CLOCK, CLOCK_GET_RATE, 4);
	CHECK(ret.error == 0 && (int)page_a.w[0] == RPMI_ERR_INVALID_PARAM,
	      "bad clock: error %ld status %d", ret.error, (int)page_a.w[0]);
	ret = send(CH_CLOCK, 0x7f, 0);
	CHECK(ret.error == 0 && (int)page_a.w[0] == RPMI_ERR_NOT_SUPPORTED,
	      "bad service: error %ld status %d", ret.error, (int)page_a.w[0]);

	/* The largest message both ways: the echo adds its STATUS word. */
	for (unsigned int i = 0; i < MAX_DATA / 4 - 1; i++)
		page_a.w[i] = 0xa5a50000 + i;
	ret = send(CH_CLOCK, PUC_TEST_ECHO, MAX_DATA - 4);
	CHECK(ret.error == 0 && ret.value == MAX_DATA,
	      "echo: error %ld, %ld bytes", ret.error, ret.value);
	for (unsigned int i = 0; i < MAX_DATA / 4 - 1; i++)
		CHECK(page_a.w[1 + i] == 0xa5a50000 + i, "echo word %u: %x", i,
		      page_a.w[1 + i]);

	CHECK_RET(send(CH_CLOCK, CLOCK_GET_NUM_CLOCKS, MAX_DATA + 4),
		  SBI_ERR_INVALID_PARAM);
	CHECK_RET(send(CH_CLOCK, CLOCK_GET_RATE, 6), SBI_ERR_INVALID_PARAM);
	CHECK_RET(send(CH_CLOCK, RPMI_SERVICE_NOTIFICATION, 0),
		  SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(send(CH_CLOCK, 0x100, 0), SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(send(CH_BOGUS, CLOCK_GET_NUM_CLOCKS, 0),
		  SBI_ERR_NOT_SUPPORTED);

	/* Posted request: no response, the PuC model shows that it arrived. */
	page_a.w[0] = 0xfeed;
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_SEND_WITHOUT_RESP, CH_CLOCK,
			    PUC_TEST_POSTED, 4),
		  SBI_SUCCESS);
	CHECK(WAIT_FOR(READ_ONCE(puc_posted_value) == 0xfeed),
	      "posted request not received");

	/* A leftover acknowledgment in the queue is skipped. */
	page_a.w[0] = 0x57a1e;
	ret = send(CH_CLOCK, PUC_TEST_STALE_ACK, 4);
	CHECK(ret.error == 0 && ret.value == 8 && page_a.w[1] == 0x57a1e,
	      "stale acknowledgment: error %ld, %ld bytes, %x", ret.error,
	      ret.value, page_a.w[1]);

	/* A PuC that does not answer: timeout, and the channel still works. */
	CHECK_RET(send(CH_CLOCK, PUC_TEST_SILENT, 0), SBI_ERR_TIMEOUT);
	ret = send(CH_CLOCK, CLOCK_GET_NUM_CLOCKS, 0);
	CHECK(ret.error == 0 && page_a.w[1] == PUC_NUM_CLOCKS,
	      "after a timeout");
}

static struct sbiret get_events(void)
{
	return sbi_call1(SBI_EXT_MPXY, FID_GET_NOTIFICATIONS, CH_CLOCK);
}

static void make_events(uint32_t count)
{
	page_a.w[0] = count;
	CHECK_RET(send(CH_CLOCK, PUC_TEST_NOTIFY, 4), SBI_SUCCESS);
}

static void test_notifications(void)
{
	const unsigned int ev_size = RPMI_EVENT_HDR_SIZE + PUC_EVENT_DATALEN;
	const unsigned int room = CONFIG_MPXY_RPMI_EVENT_BUF_SIZE / ev_size;
	struct sbiret ret = {};

	ret = get_events();
	CHECK(ret.error == 0 && ret.value == 0 && page_a.w[1] == 0,
	      "events before any: error %ld, %ld bytes", ret.error, ret.value);

	/*
	 * Disabled until the group's ENABLE_NOTIFICATION service says
	 * otherwise.
	 */
	make_events(2);
	ret = get_events();
	CHECK(ret.error == 0 && ret.value == 0, "events while disabled");
	page_a.w[0] = PUC_EVENT_ID;
	page_a.w[1] = 1;
	ret = send(CH_CLOCK, RPMI_SERVICE_ENABLE_NOTIFICATION, 8);
	CHECK(ret.error == 0 && page_a.w[0] == 0 && page_a.w[1] == 1,
	      "enable_notification");

	make_events(3);
	ret = get_events();
	CHECK_RET(ret, SBI_SUCCESS);
	CHECK(ret.value == 3 * ev_size, "%ld bytes of events", ret.value);
	CHECK(page_a.w[0] == 0 && page_a.w[1] == 3 && page_a.w[2] == 0,
	      "remaining %u returned %u lost %u", page_a.w[0], page_a.w[1],
	      page_a.w[2]);
	for (unsigned int i = 0; i < 3; i++) {
		const uint32_t *ev = &page_a.w[4 + 3 * i];

		CHECK(RPMI_EVENT_ID(ev[0]) == PUC_EVENT_ID &&
		      RPMI_EVENT_DATALEN(ev[0]) == PUC_EVENT_DATALEN &&
		      ev[1] == i && ev[2] == PUC_EVENT_MAGIC,
		      "event %u: %x %x %x", i, ev[0], ev[1], ev[2]);
	}

	/* More than the monitor buffers: the excess is counted, once. */
	make_events(room + 5);
	ret = get_events();
	CHECK(ret.error == 0 && ret.value == (long)(room * ev_size),
	      "overflow: error %ld, %ld bytes", ret.error, ret.value);
	CHECK(page_a.w[0] == 0 && page_a.w[1] == room && page_a.w[2] == 5,
	      "remaining %u returned %u lost %u", page_a.w[0], page_a.w[1],
	      page_a.w[2]);
	CHECK(page_a.w[5] == 3, "first event after the overflow: sequence %u",
	      page_a.w[5]);
	ret = get_events();
	CHECK(ret.error == 0 && ret.value == 0 && page_a.w[2] == 0,
	      "after the overflow: %ld bytes, lost %u", ret.value, page_a.w[2]);

	CHECK_RET(sbi_call1(SBI_EXT_MPXY, FID_GET_NOTIFICATIONS, CH_BOGUS),
		  SBI_ERR_NOT_SUPPORTED);
}

void test_mpxy(void)
{
	struct sbiret ret = {};

	printf("mpxy\n");
	ret = sbi_call1(SBI_EXT_BASE, SBI_BASE_PROBE_EXTENSION, SBI_EXT_MPXY);
	CHECK(ret.value != 0, "MPXY not probed");

	test_shmem();
	test_channels();
	printf("mpxy attributes\n");
	test_attributes();
	printf("mpxy messages\n");
	test_messages();
	printf("mpxy notifications\n");
	test_notifications();

	CHECK_RET(sbi_call0(SBI_EXT_MPXY, 8), SBI_ERR_NOT_SUPPORTED);
	CHECK_RET(sbi_call3(SBI_EXT_MPXY, FID_SET_SHMEM, ~UL(0), ~UL(0), 0),
		  SBI_SUCCESS);
}

#else

void test_mpxy(void)
{
}

#endif
