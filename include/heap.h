/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef HEAP_H
#define HEAP_H

/*
 * Memory for what the machine sizes rather than the build: state per hart
 * and per domain, of which the device tree says at boot how many there are.
 * It comes out of the image's own memory, between its end and the harts'
 * stacks, is handed out at boot and never given back; running out of it is
 * a configuration that does not fit (CONFIG_MONITOR_SIZE), and stops the
 * boot with a message saying so.
 */

#include <stddef.h>

/*
 * Boot hart, once the hart table is there: the stacks of 'harts' harts are
 * taken.
 */
void heap_init(unsigned int harts);
/*
 * Zeroed, aligned for anything. Boot hart only, before the other harts run C.
 */
void *heap_alloc(size_t size);
/* An array of them; the multiplication is checked. */
void *heap_alloc_array(size_t count, size_t size);
size_t heap_free_bytes(void);

#endif
