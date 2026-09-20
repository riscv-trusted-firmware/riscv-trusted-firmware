# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += reset.c
srcs-$(CONFIG_RESET_SIFIVE_TEST) += sifive_test.c
srcs-$(CONFIG_RESET_SYSCON) += syscon_reset.c
srcs-$(CONFIG_RESET_RPMI) += rpmi_reset.c
srcs-$(CONFIG_RESET_HTIF) += htif_reset.c
srcs-$(CONFIG_RESET_GPIO) += gpio_reset.c
srcs-$(CONFIG_RESET_WDT) += wdt_reset.c
