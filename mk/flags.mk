# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Global compiler/assembler/linker flags. Per-image flags live in
# images/<name>/image.mk, per-directory/per-file flags in sub.mk.

# Compiler-provided headers only (stdint.h, stdarg.h, ...); libc comes from lib/libc.
NOSTDINC_FLAGS := -nostdinc -isystem $(shell $(CC) $(ARCH_FLAGS) -print-file-name=include)

CPPFLAGS := $(NOSTDINC_FLAGS)
CPPFLAGS += -include $(AUTOCONF_H)
CPPFLAGS += -I$(SRCTREE)/include
CPPFLAGS += -I$(SRCTREE)/arch/riscv/include
CPPFLAGS += -I$(SRCTREE)/lib/libc/include
CPPFLAGS += -I$(SRCTREE)/lib/libutils/include
CPPFLAGS += -I$(SRCTREE)/platform/$(CONFIG_PLATFORM_DIR)/include
CPPFLAGS += -I$(O)/include
CPPFLAGS += -D__RISCV_XLEN__=$(XLEN)

CFLAGS := $(ARCH_FLAGS)
CFLAGS += -std=gnu11 -ffreestanding -fno-common -fno-builtin -fno-pie -fno-pic
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
CFLAGS += -Wall -Wextra -Wundef -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS += -Wredundant-decls -Wno-unused-parameter
CFLAGS += $(call cc-option,-Wno-address-of-packed-member)
ifeq ($(W),1)
CFLAGS += -Wconversion -Wcast-align -Wpointer-arith
endif
ifneq ($(CONFIG_WERROR),)
CFLAGS += -Werror
endif
ifneq ($(CONFIG_OPTIMIZE_SIZE),)
CFLAGS += -Os
else ifneq ($(CONFIG_OPTIMIZE_DEBUG),)
CFLAGS += -Og
else
CFLAGS += -O2
endif
ifneq ($(CONFIG_DEBUG_INFO),)
CFLAGS += -g3
endif
ifneq ($(CONFIG_STACK_USAGE),)
CFLAGS += -fstack-usage
endif
ifeq ($(TOOLCHAIN),gcc)
CFLAGS += -fno-delete-null-pointer-checks
endif

ASFLAGS := $(ARCH_FLAGS) -D__ASSEMBLY__ -ffreestanding

LDFLAGS := -m $(LD_EMULATION) -nostdlib -static --gc-sections
LDFLAGS += -z noexecstack -z max-page-size=4096
LDFLAGS += $(call ld-option,--no-warn-rwx-segments)
LDFLAGS += $(call ld-option,--orphan-handling=warn)
ifneq ($(CONFIG_RISCV_NO_RELAX),)
LDFLAGS += --no-relax
endif

# Compiler runtime (__udivdi3 on rv32, ...). Only linked when the toolchain has it.
LIBGCC := $(shell $(CC) $(ARCH_FLAGS) -print-libgcc-file-name 2>/dev/null)
LIBGCC := $(wildcard $(LIBGCC))
