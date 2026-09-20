# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

subdirs-y += $(patsubst $(SRCTREE)/drivers/%/sub.mk,%,$(wildcard $(SRCTREE)/drivers/*/sub.mk))
