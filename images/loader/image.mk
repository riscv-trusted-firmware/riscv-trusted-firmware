# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

images-$(CONFIG_IMAGE_LOADER) += loader

# Console only: the runtime drivers (timer, IPI, reset) need the monitor.
loader-dirs     := arch/riscv lib drivers/core drivers/serial \
		   platform/$(CONFIG_PLATFORM_DIR) images/loader
loader-ldscript := $(SRCTREE)/images/loader/loader.ld.S
loader-cppflags := -DIMAGE_LOADER
