#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause

CHECKPATCH="${CHECKPATCH:-checkpatch.pl}"
CHECKPATCH_OPT="${CHECKPATCH_OPT:-}"
# checkpatch.pl will ignore the following paths
CHECKPATCH_IGNORE=$(echo \
		lib/libfdt lib/libutils lib/builtins \
		lib/libc/include/stdint.h \
		arch/riscv/include/arch/image.lds.h )
_CP_EXCL=$(for p in $CHECKPATCH_IGNORE; do echo ":(exclude)$p" ; done)

function _checkpatch() {
		# Use --typedefsfile if supported by the checkpatch tool
		typedefs_opt="--typedefsfile typedefs.checkpatch"
		$CHECKPATCH --help 2>&1 | grep -q -- --typedefsfile || \
				typedefs_opt="";
		# Ignore NOT_UNIFIED_DIFF in case patch has no diff
		# (e.g., all paths filtered out)
		eval "$CHECKPATCH $CHECKPATCH_OPT $typedefs_opt -"
}

function checkpatch() {
	git show --oneline --no-patch $1
	# The first git 'format-patch' shows the commit message
	# The second one produces the diff (might be empty if _CP_EXCL
	# filters out all diffs)
	(git format-patch $1^..$1 --stdout | sed -n '/^diff --git/q;p'; \
	 git format-patch $1^..$1 --stdout -- $_CP_EXCL . | \
		sed -n '/^diff --git/,$p') | _checkpatch
}

function checkstaging() {
		git diff --cached -- . $_CP_EXCL | _checkpatch
}

function checkworking() {
		git diff -- . $_CP_EXCL | _checkpatch
}

function checkdiff() {
		git diff $1...$2 -- . $_CP_EXCL | _checkpatch
}

