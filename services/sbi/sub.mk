# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-$(CONFIG_SBI) += base.c
srcs-$(CONFIG_SBI) += hartmask.c
srcs-$(CONFIG_SBI) += time.c
srcs-$(CONFIG_SBI) += ipi.c
srcs-$(CONFIG_SBI) += rfence.c
srcs-$(CONFIG_SBI) += hsm.c
srcs-$(CONFIG_SBI) += srst.c
srcs-$(CONFIG_SBI) += fwft.c
srcs-$(CONFIG_SBI_SUSP) += susp.c
srcs-$(CONFIG_SBI_SSE) += sse.c
srcs-$(CONFIG_SBI_DBTR) += dbtr.c
srcs-$(CONFIG_MPXY) += mpxy.c
srcs-$(CONFIG_SBI_PMU) += pmu.c
srcs-$(CONFIG_SBI_DBCN) += dbcn.c
srcs-$(CONFIG_SBI_LEGACY) += legacy.c
