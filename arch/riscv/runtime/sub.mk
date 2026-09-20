# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# What an image needs to host a lower privilege level: per-hart state, the
# trap policy, PMP, hart state management, remote fences. Listed by the
# image (images/monitor/image.mk), not pulled in by arch/riscv/sub.mk.
srcs-$(CONFIG_SBI_DBTR) += dbtr.c
srcs-$(CONFIG_DOMAINS) += domain.c
srcs-$(CONFIG_DOMAINS) += domain_context.c
srcs-$(CONFIG_DOMAINS) += domain_state.S
srcs-y += fwft.c
srcs-y += hart.c
srcs-y += hsm.c
srcs-y += illegal_insn.c
srcs-y += isa.c
srcs-y += misaligned.c
srcs-y += misaligned_vector.c
srcs-y += pmp.c
srcs-$(CONFIG_SBI_PMU) += pmu.c
srcs-y += rfence.c
srcs-$(CONFIG_SBI_SSE) += sse.c
srcs-y += switch.S
srcs-y += trap.c
srcs-y += unpriv.c
