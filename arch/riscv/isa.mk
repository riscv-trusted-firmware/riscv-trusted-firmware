# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Derive -march/-mabi and the target-specific compiler flags from Kconfig.
# Every ISA extension the compiler is allowed to emit code for is a
# CONFIG_RISCV_ISA_* symbol (arch/riscv/Kconfig); runtime-only extensions
# (Smepmp, Sstc, ...) are CONFIG_RISCV_EXT_* and do not appear in -march.

XLEN := $(CONFIG_RISCV_XLEN)

march-base := rv$(XLEN)i
march-base := $(march-base)$(if $(CONFIG_RISCV_ISA_M),m)
march-base := $(march-base)$(if $(CONFIG_RISCV_ISA_A),a)
march-base := $(march-base)$(if $(CONFIG_RISCV_ISA_F),f)
march-base := $(march-base)$(if $(CONFIG_RISCV_ISA_D),d)
march-base := $(march-base)$(if $(CONFIG_RISCV_ISA_C),c)

# Z-extensions, in canonical order.
march-z-y :=
march-z-$(CONFIG_RISCV_ISA_ZICSR)     += zicsr
march-z-$(CONFIG_RISCV_ISA_ZIFENCEI)  += zifencei
march-z-$(CONFIG_RISCV_ISA_ZICBOM)    += zicbom
march-z-$(CONFIG_RISCV_ISA_ZICBOZ)    += zicboz
march-z-$(CONFIG_RISCV_ISA_ZIHINTPAUSE) += zihintpause
march-z-$(CONFIG_RISCV_ISA_ZBA)       += zba
march-z-$(CONFIG_RISCV_ISA_ZBB)       += zbb
march-z-$(CONFIG_RISCV_ISA_ZBC)       += zbc
march-z-$(CONFIG_RISCV_ISA_ZBS)       += zbs

MARCH := $(march-base)$(subst $(space),,$(addprefix _,$(march-z-y)))$(CONFIG_RISCV_ISA_EXTRA)
MABI  := $(CONFIG_RISCV_ABI)

ARCH_FLAGS := -march=$(MARCH) -mabi=$(MABI) -mcmodel=medany
ARCH_FLAGS += $(if $(CONFIG_RISCV_STRICT_ALIGN),-mstrict-align)
ARCH_FLAGS += $(if $(CONFIG_RISCV_NO_RELAX),-mno-relax)

ifeq ($(TOOLCHAIN),clang)
ARCH_FLAGS += --target=riscv$(XLEN)-unknown-elf
endif

LD_EMULATION := elf$(XLEN)lriscv
