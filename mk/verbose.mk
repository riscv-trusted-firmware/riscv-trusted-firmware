# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Output control. V=1 prints full commands; default prints "  CC  file".

# Usage in a recipe line:  $(call cmd,CC,$@) $(CC) ... ; the leading '@' in
# the quiet form hides the whole line, the verbose form hides nothing.
ifeq ($(V),1)
q :=
cmd =
else
q := @
cmd = @printf '  %-8s %s\n' '$(1)' '$(patsubst $(SRCTREE)/%,%,$(patsubst $(O)/%,%,$(2)))';
endif

empty :=
space := $(empty) $(empty)
comma := ,

# $(call unquote,"str") -> str
unquote = $(patsubst "%",%,$(1))
