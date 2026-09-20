# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

images-$(CONFIG_IMAGE_MONITOR) += monitor

monitor-dirs     := arch/riscv lib drivers services \
		    platform/$(CONFIG_PLATFORM_DIR) images/monitor
monitor-ldscript := $(SRCTREE)/images/monitor/monitor.ld.S
monitor-cppflags := -DIMAGE_MONITOR
