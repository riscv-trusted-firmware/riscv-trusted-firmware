/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef LOG_H
#define LOG_H

#include <compiler.h>
#include <stdio.h>

#define LOG_NONE 0
#define LOG_ERR 1
#define LOG_WARN 2
#define LOG_INFO 3
#define LOG_DBG 4

#define pr_level(lvl, tag, fmt, ...)                    \
	do {                                            \
		if (CONFIG_LOG_LEVEL >= (lvl))          \
			printf(tag fmt, ##__VA_ARGS__); \
	} while (0)

#define pr_err(fmt, ...) pr_level(LOG_ERR, "E: ", fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) pr_level(LOG_WARN, "W: ", fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) pr_level(LOG_INFO, "", fmt, ##__VA_ARGS__)
#define pr_dbg(fmt, ...) pr_level(LOG_DBG, "D: ", fmt, ##__VA_ARGS__)

void __noreturn panic(const char *fmt, ...) __printf(1, 2);

#define assert(x)                                                     \
	do {                                                          \
		if (unlikely(!(x)))                                   \
			panic("assertion '%s' failed at %s:%d\n", #x, \
			      __FILE__, __LINE__);                    \
	} while (0)

#endif
