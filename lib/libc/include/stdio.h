/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef STDIO_H
#define STDIO_H

#include <compiler.h>
#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...) __printf(1, 2);
int puts(const char *s);

#endif
