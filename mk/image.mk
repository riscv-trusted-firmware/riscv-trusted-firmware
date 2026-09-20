# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Multi-image build. Each images/<name>/image.mk registers itself with
#
#   images-$(CONFIG_IMAGE_<NAME>) += <name>
#   <name>-dirs     := <source directories, relative to the tree, each with a sub.mk>
#   <name>-ldscript := <linker script, run through the C preprocessor>
#   <name>-cflags / -asflags / -cppflags / -ldflags := <image-wide extra flags>
#
# and gets $(O)/images/<name>/<name>.{elf,bin,map,ld} plus obj/ underneath.

include $(SRCTREE)/mk/subdir.mk

VERSION_H := $(O)/include/generated/version.h
GIT_DESC  := $(shell git -C $(SRCTREE) describe --always --dirty --tags 2>/dev/null)
VERSION_STRING := $(PROJECT_VERSION)$(if $(GIT_DESC), ($(GIT_DESC)))

# Rewritten only when the content changes, so it does not force rebuilds.
.PHONY: FORCE
$(VERSION_H): FORCE
	$(q)mkdir -p $(@D)
	$(q)printf '#define PROJECT_NAME "%s"\n#define PROJECT_VERSION "%s"\n#define BUILD_TARGET "%s"\n' \
		'$(PROJECT_NAME)' '$(VERSION_STRING)' '$(MARCH) $(MABI) $(TOOLCHAIN)' > $@.tmp
	$(q)cmp -s $@.tmp $@ || { $(if $(q),,echo "  GEN      $(patsubst $(O)/%,%,$@)";) mv $@.tmp $@; }
	$(q)rm -f $@.tmp

images-y :=
images-targets :=
images-dumps :=
include $(wildcard $(SRCTREE)/images/*/image.mk)

# $(call build-image,<name>)
define build-image
$(1)-objs :=
$(1)-global-incdirs :=
$(1)-sub-mk :=
$$(foreach d,$$($(1)-dirs),$$(eval $$(call process-subdir,$(1),$$(d))))
$$(if $$(filter-out $$(words $$(sort $$($(1)-objs))),$$(words $$($(1)-objs))),\
	$$(error image $(1): two sources map to the same object file (same basename, \
	different extension?): $$(sort $$(filter $$(sort $$($(1)-objs)),$$($(1)-objs)))))

$(1)-out := $(O)/images/$(1)
$(1)-elf := $$($(1)-out)/$(1).elf
$(1)-bin := $$($(1)-out)/$(1).bin
$(1)-ld  := $$($(1)-out)/$(1).ld
$(1)-map := $$($(1)-out)/$(1).map
$(1)-dump := $$($(1)-out)/$(1).dump
images-targets += $$($(1)-elf) $$($(1)-bin)
images-dumps += $$($(1)-dump)

$$($(1)-out)/obj/%.o: $(SRCTREE)/%.c $$($(1)-sub-mk) | $(VERSION_H)
	$(q)mkdir -p $$(@D)
	$$(call cmd,CC,$$<) $$(CC) $$(CPPFLAGS) $$($(1)-global-incdirs) $$($(1)-cppflags) \
		$$(obj-cppflags) $$(CFLAGS) $$($(1)-cflags) $$(obj-cflags) \
		-MMD -MP -MF $$@.d -c $$< -o $$@

$$($(1)-out)/obj/%.o: $(SRCTREE)/%.S $$($(1)-sub-mk) | $(VERSION_H)
	$(q)mkdir -p $$(@D)
	$$(call cmd,AS,$$<) $$(CC) $$(CPPFLAGS) $$($(1)-global-incdirs) $$($(1)-cppflags) \
		$$(obj-cppflags) $$(ASFLAGS) $$($(1)-asflags) $$(obj-asflags) \
		-MMD -MP -MF $$@.d -c $$< -o $$@

$$($(1)-ld): $$($(1)-ldscript) $(AUTOCONF_H) $(SRCTREE)/images/$(1)/image.mk
	$(q)mkdir -p $$(@D)
	$$(call cmd,LDS,$$<) $$(CC) -E -P -x assembler-with-cpp $$(CPPFLAGS) \
		$$($(1)-global-incdirs) $$($(1)-cppflags) -D__LINKER__ \
		-MMD -MP -MT $$@ -MF $$@.d $$< -o $$@

$$($(1)-elf): $$($(1)-objs) $$($(1)-ld)
	$$(call cmd,LD,$$@) $$(LD) $$(LDFLAGS) $$($(1)-ldflags) -T $$($(1)-ld) \
		-Map $$($(1)-map) -o $$@ --start-group $$($(1)-objs) $$(LIBGCC) --end-group
	$(q)$$(SIZE) $$@ | sed -n '2s/^/  SIZE     /p'

$$($(1)-bin): $$($(1)-elf)
	$$(call cmd,BIN,$$@) $$(OBJCOPY) -O binary $$< $$@

$$($(1)-dump): $$($(1)-elf)
	$$(call cmd,DUMP,$$@) $$(OBJDUMP) -d -S $$< > $$@

-include $$($(1)-objs:.o=.o.d) $$($(1)-ld).d
endef

$(foreach i,$(images-y),$(eval $(call build-image,$(i))))

ifeq ($(images-y),)
$(error No image enabled; enable CONFIG_IMAGE_* in menuconfig)
endif
