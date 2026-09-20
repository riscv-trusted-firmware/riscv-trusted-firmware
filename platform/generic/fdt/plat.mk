# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Generic platform: 'make run' boots the build on a QEMU machine of choice
# (QEMU_MACHINE=virt, spike, sifive_u), which is where it gets its device
# tree from, with the S-mode test payload for a next stage when it is built.

QEMU      ?= qemu-system-riscv$(XLEN)
QEMU_SMP  ?= 4
QEMU_MACHINE ?= virt
QEMU_MEM  ?= 256
QEMU_ARGS ?=
QEMU_KERNEL ?=

qemu-images = -bios $(1)/images/monitor/monitor.bin
ifneq ($(QEMU_KERNEL),)
qemu-images += -kernel $(QEMU_KERNEL)
else ifneq ($(CONFIG_IMAGE_SBITEST),)
qemu-images += -device loader,file=$(1)/images/sbitest/sbitest.bin,addr=$(CONFIG_SBITEST_LOAD_ADDR)
endif

define plat-run
	$(QEMU) -M $(QEMU_MACHINE) -nographic -smp $(QEMU_SMP) -m $(QEMU_MEM) \
		$(call qemu-images,$(1)) $(QEMU_ARGS)
endef
