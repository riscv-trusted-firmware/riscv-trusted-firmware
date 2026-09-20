# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += suspend.c
srcs-$(CONFIG_SUSPEND_RPMI) += rpmi_suspend.c
