# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += main.c
# The loader carries no service table; keep the arch trap path linkable.
srcs-y += no_services.c
