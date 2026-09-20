# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

subdirs-y += $(patsubst $(SRCTREE)/lib/%/sub.mk,%,$(wildcard $(SRCTREE)/lib/*/sub.mk))
