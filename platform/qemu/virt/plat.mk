# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# QEMU virt: Make hooks. $(call plat-run,<build dir>) boots the build.

QEMU      ?= qemu-system-riscv$(XLEN)
QEMU_SMP  ?= 4
QEMU_ARGS ?=

# -bios takes the loader when it is enabled (it jumps to the monitor loaded
# by the generic loader device), the monitor otherwise.
ifneq ($(CONFIG_IMAGE_LOADER),)
qemu-images = -bios $(1)/images/loader/loader.bin \
	      -device loader,file=$(1)/images/monitor/monitor.bin,addr=$(CONFIG_LOADER_NEXT_STAGE_ADDR)
else
qemu-images = -bios $(1)/images/monitor/monitor.bin
endif

define plat-run
	$(QEMU) -M virt -nographic -smp $(QEMU_SMP) -m $$(($(CONFIG_QEMU_VIRT_RAM_SIZE)/1048576)) \
		$(call qemu-images,$(1)) $(QEMU_ARGS)
endef
