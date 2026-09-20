# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += timer.c
srcs-$(CONFIG_TIMER_ACLINT_MTIMER) += aclint_mtimer.c
