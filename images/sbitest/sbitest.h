/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef SBITEST_H
#define SBITEST_H

/* start.S, the trap entry and main.c */
void test_main(unsigned long hartid, unsigned long fdt);
void secondary_main(unsigned long hartid, unsigned long opaque);
void resume_main(unsigned long hartid, unsigned long opaque);
unsigned long strap_handler(unsigned long scause, unsigned long sepc,
			    unsigned long stval);
void _secondary_start(void);
void _resume_start(void);

#endif
