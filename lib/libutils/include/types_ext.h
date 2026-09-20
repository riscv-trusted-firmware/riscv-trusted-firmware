/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef TYPES_EXT_H
#define TYPES_EXT_H

/*
 * Types that say what a number is. An address is an unsigned long on
 * every RISC-V this runs on, and nothing stops anybody from writing that;
 * but whether a function takes an address the monitor can dereference, or
 * one that a lower privilege level named and that has to be checked first,
 * is worth a word in its prototype.
 *
 *   vaddr_t   an address the running code can use as it is. M-mode runs
 *             untranslated, so for the monitor itself this is a physical
 *             address too: its own memory, its devices.
 *   paddr_t   a physical address as such: one S-mode passed, one from the
 *             device tree, the base of a PMP region. Not to be touched
 *             before somebody has decided that it may be.
 *   uaddr_t   an address of a lower privilege level's address space, to be
 *             reached through an unprivileged access if at all.
 *
 * They print with %lx; the PRIx macros are there for code that would
 * rather not know.
 *
 * Nothing of this means anything to an assembler, which may well get to see
 * the file through a header it shares with C.
 */

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned long uaddr_t;
#define PRIxUA "lx"

typedef unsigned long vaddr_t;
#define PRIxVA "lx"

typedef unsigned long paddr_t;
typedef unsigned long paddr_size_t;
#define PADDR_MAX (~0UL)
#define PADDR_SIZE_MAX (~0UL)
#define PRIxPA "lx"
#define PRIxPASZ "lx"
#define __SIZEOF_PADDR__ __SIZEOF_LONG__

/* Field widths for an address in full, zeros included. */
#define PRIxVA_WIDTH ((int)(sizeof(vaddr_t) * 2))
#define PRIxPA_WIDTH ((int)(sizeof(paddr_t) * 2))

#endif /* !__ASSEMBLER__ */

#endif
