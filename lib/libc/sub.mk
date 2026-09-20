# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-$(CONFIG_LIBC) += string.c
srcs-$(CONFIG_LIBC) += printf.c
# The compiler may inline calls to these back into themselves.
cflags-string.c-y += $(call cc-option,-fno-tree-loop-distribute-patterns)
