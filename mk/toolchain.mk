# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Toolchain selection.
#   default : GCC, prefix $(CROSS_COMPILE) (default riscv64-unknown-elf-)
#   LLVM=1  : clang + ld.lld + llvm-* ; LLVM=<path/> or LLVM=-<suffix> as in Linux

ifeq ($(LLVM),)
CROSS_COMPILE ?= riscv64-unknown-elf-
CC       := $(CROSS_COMPILE)gcc
LD       := $(CROSS_COMPILE)ld
AR       := $(CROSS_COMPILE)ar
NM       := $(CROSS_COMPILE)nm
OBJCOPY  := $(CROSS_COMPILE)objcopy
OBJDUMP  := $(CROSS_COMPILE)objdump
SIZE     := $(CROSS_COMPILE)size
TOOLCHAIN := gcc
else
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif
CC       := $(LLVM_PREFIX)clang$(LLVM_SUFFIX)
LD       := $(LLVM_PREFIX)ld.lld$(LLVM_SUFFIX)
AR       := $(LLVM_PREFIX)llvm-ar$(LLVM_SUFFIX)
NM       := $(LLVM_PREFIX)llvm-nm$(LLVM_SUFFIX)
OBJCOPY  := $(LLVM_PREFIX)llvm-objcopy$(LLVM_SUFFIX)
OBJDUMP  := $(LLVM_PREFIX)llvm-objdump$(LLVM_SUFFIX)
SIZE     := $(LLVM_PREFIX)llvm-size$(LLVM_SUFFIX)
TOOLCHAIN := clang
endif

CC_VERSION := $(shell $(CC) --version 2>/dev/null | head -1)
ifeq ($(CC_VERSION),)
$(error Compiler '$(CC)' not found; set CROSS_COMPILE=<prefix> or LLVM=1)
endif

# $(call cc-option,flag[,alternative]) / $(call ld-option,flag[,alternative])
cc-option = $(shell if $(CC) $(ARCH_FLAGS) -Werror $(1) -c -x c /dev/null \
	      -o /dev/null >/dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)
ld-option = $(shell if $(LD) -v $(1) >/dev/null 2>&1; then echo "$(1)"; \
	      else echo "$(2)"; fi)
