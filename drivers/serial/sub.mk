# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

srcs-y += serial.c
srcs-$(CONFIG_SERIAL_UART8250) += uart8250.c
srcs-$(CONFIG_SERIAL_SIFIVE) += sifive_uart.c
srcs-$(CONFIG_SERIAL_HTIF) += htif.c
srcs-$(CONFIG_SERIAL_UARTLITE) += uartlite.c
srcs-$(CONFIG_SERIAL_CADENCE) += cadence_uart.c
srcs-$(CONFIG_SERIAL_LITEX) += litex_uart.c
srcs-$(CONFIG_SERIAL_GAISLER) += gaisler_uart.c
srcs-$(CONFIG_SERIAL_SHAKTI) += shakti_uart.c
srcs-$(CONFIG_SERIAL_RENESAS_SCIF) += renesas_scif.c
srcs-$(CONFIG_SERIAL_SEMIHOSTING) += semihosting.c
