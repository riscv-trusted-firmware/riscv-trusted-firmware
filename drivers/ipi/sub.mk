# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += ipi.c
srcs-$(CONFIG_IPI_ACLINT_MSWI) += aclint_mswi.c
srcs-$(CONFIG_IPI_ANDES_PLICSW) += andes_plicsw.c
