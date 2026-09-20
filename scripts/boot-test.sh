#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors
# Usage: [MAKEARGS="LLVM=1 ..."] [QEMU_EXTRA_ARGS="-cpu rv64,sstc=off"]
#        <this script> <build dir> [expected string]
# Boots the build on QEMU (make run) with the serial on a file and checks
# that the expected string shows up within a few seconds. The default is the
# verdict of the SBI test payload when the build has it (IMAGE_SBITEST), the
# monitor's idle message otherwise.
set -e
O=${1:?build dir}
if grep -q '^CONFIG_IMAGE_SBITEST=y' "$O/.config" 2>/dev/null; then
	EXPECT=${2:-"sbitest: PASS"}
else
	EXPECT=${2:-"monitor: idle"}
fi
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
timeout 60 make -s O="$O" $MAKEARGS run \
	QEMU_ARGS="-serial file:$LOG -monitor none $QEMU_EXTRA_ARGS" \
	>/dev/null 2>&1 || true
if grep -q "$EXPECT" "$LOG"; then
	echo "boot-test: OK ($O)"; sed 's/^/  | /' "$LOG"
else
	echo "boot-test: FAILED ($O), expected '$EXPECT'"
	sed 's/^/  | /' "$LOG"
	exit 1
fi
