# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-$(CONFIG_IRQCHIP) += irqchip.c
srcs-$(CONFIG_IRQCHIP_PLIC) += plic.c
srcs-$(CONFIG_IRQCHIP_APLIC) += aplic.c
srcs-$(CONFIG_IRQCHIP_IMSIC) += imsic.c
