# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# sub.mk processing. Every source directory has a sub.mk that may set:
#
#   srcs-y          += foo.c bar.S          sources (relative to the directory)
#   subdirs-y       += dir                  sub-directories with their own sub.mk
#   incdirs-y       += include              -I for this directory's objects only
#   global-incdirs-y+= include              -I for every object of the image
#   global-sysincdirs-y += include          same with -isystem: headers of imported
#                                           code, exempt from the tree's warning set
#   cflags-y        += -Dx                  extra CFLAGS for this directory
#   cflags-foo.c-y  += -Dx                  extra CFLAGS for one file
#   asflags-y / asflags-foo.S-y             same for assembly
#   cppflags-y                              preprocessor flags (C, asm, linker script)
#
# Any of these accepts a Kconfig-driven suffix: srcs-$(CONFIG_FOO) += foo.c.
# Objects land in $(O)/images/<image>/obj/<srcdir>/<file>.o.

# $(call process-src,<image>,<dir>,<src>)
define process-src
$(1)-objs += $(O)/images/$(1)/obj/$(2)/$(basename $(3)).o
$(O)/images/$(1)/obj/$(2)/$(basename $(3)).o: $(SRCTREE)/$(2)/$(3)
$(O)/images/$(1)/obj/$(2)/$(basename $(3)).o: private obj-cflags := $$(cflags-y) $$(cflags-$(3)-y)
$(O)/images/$(1)/obj/$(2)/$(basename $(3)).o: private obj-asflags := $$(asflags-y) $$(asflags-$(3)-y)
$(O)/images/$(1)/obj/$(2)/$(basename $(3)).o: private obj-cppflags := $$(cppflags-y) $$(addprefix -I$(SRCTREE)/$(2)/,$$(incdirs-y))
endef

# $(call process-subdir,<image>,<dir>)
define process-subdir
srcs-y :=
subdirs-y :=
incdirs-y :=
global-incdirs-y :=
global-sysincdirs-y :=
cflags-y :=
asflags-y :=
cppflags-y :=
include $(SRCTREE)/$(2)/sub.mk
$(1)-global-incdirs += $$(addprefix -I$(SRCTREE)/$(2)/,$$(global-incdirs-y))
$(1)-global-incdirs += $$(addprefix -isystem $(SRCTREE)/$(2)/,$$(global-sysincdirs-y))
$(1)-sub-mk += $(SRCTREE)/$(2)/sub.mk
$$(foreach s,$$(srcs-y),$$(eval $$(call process-src,$(1),$(2),$$(s))))
$$(foreach d,$$(subdirs-y),$$(eval $$(call process-subdir,$(1),$(2)/$$(d))))
endef
