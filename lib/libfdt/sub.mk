# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

# Upstream sources, unmodified (see README): built with their own warning set.
global-sysincdirs-$(CONFIG_LIBFDT) += include
cflags-y += -Wno-conversion -Wno-sign-conversion -Wno-cast-align -Wno-pointer-arith

srcs-$(CONFIG_LIBFDT) += fdt.c
srcs-$(CONFIG_LIBFDT) += fdt_addresses.c
srcs-$(CONFIG_LIBFDT) += fdt_check.c
srcs-$(CONFIG_LIBFDT) += fdt_ro.c
srcs-$(CONFIG_LIBFDT) += fdt_rw.c
srcs-$(CONFIG_LIBFDT) += fdt_strerror.c
srcs-$(CONFIG_LIBFDT) += fdt_sw.c
srcs-$(CONFIG_LIBFDT) += fdt_wip.c
