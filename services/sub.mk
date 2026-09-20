# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

# Every services/<name>/sub.mk is picked up; each one gates its own sources.
subdirs-y += $(patsubst $(SRCTREE)/services/%/sub.mk,%,$(wildcard $(SRCTREE)/services/*/sub.mk))
