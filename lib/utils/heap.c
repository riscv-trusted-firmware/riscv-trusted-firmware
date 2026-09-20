// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/* The boot heap, see <heap.h>. */

#ifdef IMAGE_HEAP

#include <arch/image.h>
#include <heap.h>
#include <log.h>
#include <stdint.h>
#include <string.h>
#include <util.h>

static uintptr_t next, limit;

void heap_init(unsigned int harts)
{
	uintptr_t top = (uintptr_t)__image_start + CONFIG_MONITOR_SIZE;

	next = (uintptr_t)__heap_start;
	limit = top - (uintptr_t)harts * CONFIG_STACK_SIZE;
	if (limit < next || limit > top)
		panic("no room for the stacks of %u harts\n", harts);
}

void *heap_alloc(size_t size)
{
	void *p = (void *)next;

	size = ROUNDUP2(size, 16);
	if (!next || size > limit - next)
		panic("out of memory (%lu bytes wanted, %lu left): CONFIG_MONITOR_SIZE\n",
		      (unsigned long)size, (unsigned long)heap_free_bytes());
	next += size;
	return memset(p, 0, size);
}

void *heap_alloc_array(size_t count, size_t size)
{
	if (size && count > (size_t)-1 / size)
		panic("out of memory (%lu x %lu bytes)\n", (unsigned long)count,
		      (unsigned long)size);
	return heap_alloc(count * size);
}

size_t heap_free_bytes(void)
{
	return limit - next;
}

#endif
