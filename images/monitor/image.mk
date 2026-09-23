# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

images-$(CONFIG_IMAGE_MONITOR) += monitor

monitor-dirs     := arch/riscv arch/riscv/runtime lib drivers services \
		    platform/$(CONFIG_PLATFORM_DIR) images/monitor
monitor-ldscript := $(SRCTREE)/images/monitor/monitor.ld.S
monitor-cppflags := -DIMAGE_MONITOR -DIMAGE_HEAP

# Position-independent: linked for MONITOR_LOAD_ADDR, runs wherever it is put.
# That takes a linker that can make a PIE: ld.lld, or GNU ld built with
# shared library support, which the bare-metal binutils of a distribution
# often is not (LD=riscv64-linux-gnu-ld is one that is). Without one the
# monitor is linked for MONITOR_LOAD_ADDR alone, and the build says so.
ifneq ($(CONFIG_MONITOR_PIE),)
ifneq ($(call ld-option,-pie),)
monitor-cppflags += -DIMAGE_PIE -DIMAGE_LINK_ADDR=CONFIG_MONITOR_LOAD_ADDR
# C only: in assembly 'la' must stay PC-relative, the GOT is not usable
# before the relocations are applied.
monitor-cflags   := -fpie
monitor-ldflags  := -pie --no-dynamic-linker -z notext
else
$(warning $(LD) cannot link a position-independent image: the monitor is linked for MONITOR_LOAD_ADDR alone)
endif
endif
