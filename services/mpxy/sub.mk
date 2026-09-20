# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-$(CONFIG_MPXY) += mpxy.c
srcs-$(CONFIG_MPXY_RPMI) += mpxy_rpmi.c
srcs-$(CONFIG_MPXY_RPMI) += mpxy_rpmi_groups.c
srcs-$(CONFIG_MPXY_RPMI_FW) += mpxy_rpmi_fw.c
srcs-$(CONFIG_MPXY_RPMI_FW) += reqfwd.c
