# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

# Built when asked for, or when the toolchain has no runtime library for the target.
builtins-y := $(if $(LIBGCC),$(CONFIG_LIB_BUILTINS),y)
srcs-$(builtins-y) += divdi3.c
srcs-$(builtins-y) += shdi3.c
