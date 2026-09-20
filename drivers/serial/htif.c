// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * HTIF ("ucb,htif0"), see <htif.h>. A request is (device, command, data) in
 * one word written to tohost; the host answers in fromhost, which is
 * written back to zero to take the answer. One request at a time, from
 * whichever hart: a lock.
 *
 * The node usually says nothing of where the words are, because a
 * simulator finds them by their symbols in the ELF it loads. It finds
 * nothing in a raw binary; CONFIG_SERIAL_HTIF_BASE is then where it puts
 * them, fromhost first and tohost 8 bytes after it, which is also how a
 * node with a "reg" is read.
 */

#include <console.h>
#include <htif.h>
#include <io.h>
#include <memregion.h>
#include <serial.h>
#include <spinlock.h>
#include <types_ext.h>
#include <util.h>

#define DEV_SYSTEM 0
#define DEV_CONSOLE 1
#define CMD_GETC 0
#define CMD_PUTC 1

#define REQUEST(dev, cmd, data)                    \
	(SHIFT_U64(dev, 56) | SHIFT_U64(cmd, 48) | \
	 ((uint64_t)(data) & ULL(0xffffffffffff)))
#define ANSWER_DEV(v) ((unsigned int)((v) >> 56))
#define ANSWER_CMD(v) ((unsigned int)((v) >> 48) & 0xff)

static uintptr_t fromhost = CONFIG_SERIAL_HTIF_BASE,
		 tohost = CONFIG_SERIAL_HTIF_BASE + 8;
static unsigned long htif_lock = SPINLOCK_UNLOCK;
/* A character that arrived while something else was waited for, + 1. */
static int console_buf;

/* The halves in this order: the host acts on the upper one. */
static void word_write(vaddr_t addr, uint64_t val)
{
	io_write32(addr, (uint32_t)val);
	io_write32(addr + 4, high32_from_64(val));
}

static uint64_t word_read(vaddr_t addr)
{
	return reg_pair_to_64(io_read32(addr + 4), io_read32(addr));
}

static void check_fromhost(void)
{
	uint64_t val = word_read(fromhost);

	if (!val)
		return;
	word_write(fromhost, 0);
	if (ANSWER_DEV(val) == DEV_CONSOLE && ANSWER_CMD(val) == CMD_GETC)
		console_buf = 1 + (int)(val & 0xff);
}

static void request(uint64_t val)
{
	/* The previous request is the host's until tohost reads zero again. */
	while (word_read(tohost))
		check_fromhost();
	word_write(tohost, val);
}

void htif_setup(uintptr_t to, uintptr_t from)
{
	tohost = to;
	fromhost = from;
}

void htif_putc(char c)
{
	spin_lock(&htif_lock);
	request(REQUEST(DEV_CONSOLE, CMD_PUTC, (uint8_t)c));
	spin_unlock(&htif_lock);
}

int htif_getc(void)
{
	int c = 0;

	spin_lock(&htif_lock);
	check_fromhost();
	c = console_buf - 1;
	console_buf = 0;
	/*
	 * Ask for the next one; the answer is there the next time round, or
	 * later.
	 */
	if (c >= 0 || !word_read(tohost))
		request(REQUEST(DEV_CONSOLE, CMD_GETC, 0));
	spin_unlock(&htif_lock);
	return c;
}

void htif_exit(unsigned int code)
{
	spin_lock(&htif_lock);
	request(REQUEST(DEV_SYSTEM, 0, SHIFT_U64(code, 1) | 1));
	spin_unlock(&htif_lock);
}

static const struct console_ops htif_console = {
	.name = "htif",
	.putc = htif_putc,
	.getc = htif_getc,
};

static int htif_serial_init(const struct serial_params *p)
{
	/* A node there is ("ucb,htif0"), with "reg" or, mostly, without. */
	if (!p)
		return -1;
	if (p->base)
		htif_setup(p->base + 8, p->base);
	if (!fromhost)
		return -1;
	console_register(&htif_console);
	/* A simulator's device, which the next stage may know as well. */
	if (!p->base)
		memregion_add(fromhost, 16, MEMREGION_SHARED_RW);
	return 0;
}

static const char *const htif_compatible[] = { "ucb,htif0", NULL };

SERIAL_DEFINE(htif_serial) = {
	.name = "htif",
	.compatible = htif_compatible,
	.init = htif_serial_init,
};
