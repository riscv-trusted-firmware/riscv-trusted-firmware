# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Top-level Makefile.
#
#   make qemu_virt_rv64_defconfig   select a configuration (see configs/)
#   make [-j]                       build every enabled image
#   make menuconfig                 edit the configuration
#   make run                        boot the build on the platform simulator
#   make help                       full target list
#
# Variables: O=<dir> (build dir), V=1 (verbose), LLVM=1 (clang/lld),
#            CROSS_COMPILE=<prefix> (GCC toolchain prefix), W=1 (extra warnings)

PROJECT_NAME    := RISC-V Trusted Firmware
PROJECT_VERSION := 0.1.0

MAKEFLAGS += -rR --no-print-directory
.SUFFIXES:
.DELETE_ON_ERROR:

SRCTREE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
O       ?= $(SRCTREE)/build
override O := $(abspath $(O))
export SRCTREE O PROJECT_NAME PROJECT_VERSION

# First rule = default goal; must precede any included makefile.
.PHONY: all
all:

include $(SRCTREE)/mk/verbose.mk
include $(SRCTREE)/mk/config.mk

ifeq ($(config-only),)
ifeq ($(wildcard $(O)/.config),)
$(error No configuration in $(O). Run 'make <name>_defconfig' (see 'make help'))
endif

include $(SRCTREE)/mk/toolchain.mk
include $(SRCTREE)/arch/riscv/isa.mk
include $(SRCTREE)/mk/flags.mk
include $(SRCTREE)/platform/$(CONFIG_PLATFORM_DIR)/plat.mk
include $(SRCTREE)/mk/image.mk

all: $(images-targets)

.PHONY: dump
dump: $(images-dumps)

.PHONY: run
run: all
	$(call plat-run,$(O))

.PHONY: check-toolchain
check-toolchain:
	@echo "CC      : $(CC) ($(CC_VERSION))"
	@echo "LD      : $(LD)"
	@echo "ARCH    : -march=$(MARCH) -mabi=$(MABI)"
	@echo "PLATFORM: $(CONFIG_PLATFORM_NAME) (platform/$(CONFIG_PLATFORM_DIR))"
	@echo "IMAGES  : $(images-y)"
endif

.PHONY: clean distclean
clean:
	$(call cmd,CLEAN,$(O)/images) rm -rf $(O)/images $(O)/include/generated/version.h
distclean:
	$(call cmd,CLEAN,$(O)) rm -rf $(O)

.PHONY: help
help:
	@echo "Configuration:"
	@for f in $(notdir $(wildcard $(SRCTREE)/configs/*_defconfig)); do echo "  $$f"; done
	@echo "  menuconfig / olddefconfig / savedefconfig"
	@echo "Build:"
	@echo "  all (default)    build enabled images into O=$(O)"
	@echo "  dump             disassembly of every image"
	@echo "  run              run on the platform simulator (if the platform provides one)"
	@echo "  check-toolchain  print the resolved toolchain and target"
	@echo "  clean / distclean"
	@echo "Options: O=<dir> V=1 W=1 LLVM=1 CROSS_COMPILE=<prefix>"
