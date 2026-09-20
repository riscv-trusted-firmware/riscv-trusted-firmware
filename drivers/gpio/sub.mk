# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-$(CONFIG_GPIO) += gpio.c
srcs-$(CONFIG_GPIO_SIFIVE) += sifive_gpio.c
