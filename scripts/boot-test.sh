#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors
# Usage: [MAKEARGS="LLVM=1 ..."] scripts/boot-test.sh <build dir> [expected string]
# Boots the build on QEMU (make run) with the serial on a file and checks
# that the expected string shows up within a few seconds.
set -e
O=${1:?build dir}
EXPECT=${2:-"monitor: idle"}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
timeout 10 make -s O="$O" $MAKEARGS run QEMU_ARGS="-serial file:$LOG -monitor none" >/dev/null 2>&1 || true
if grep -q "$EXPECT" "$LOG"; then
	echo "boot-test: OK ($O)"; sed 's/^/  | /' "$LOG"
else
	echo "boot-test: FAILED ($O), expected '$EXPECT'"
	sed 's/^/  | /' "$LOG"
	exit 1
fi
