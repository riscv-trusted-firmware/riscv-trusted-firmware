# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

images-$(CONFIG_IMAGE_SBITEST) += sbitest

# S-mode: none of the M-mode arch code, only the libraries.
sbitest-dirs     := lib images/sbitest
sbitest-ldscript := $(SRCTREE)/images/sbitest/sbitest.ld.S
sbitest-cppflags := -DIMAGE_SBITEST
