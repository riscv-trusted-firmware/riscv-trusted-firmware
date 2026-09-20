/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef HTIF_H
#define HTIF_H

/*
 * The host-target interface of Spike and its relatives: two 64-bit words
 * of memory, tohost and fromhost, through which the simulator's host side
 * is asked for things. A console and a way out of the simulation.
 */

#include <compiler.h>
#include <stdint.h>

/* Where the words are; without this, the Kconfig default. */
void htif_setup(uintptr_t tohost, uintptr_t fromhost);
void htif_putc(char c);
int htif_getc(void);
/* Ends the simulation; returns if the host does not go along. */
void htif_exit(unsigned int code);

#endif
