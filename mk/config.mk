# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors

#
# Kconfig integration (kconfiglib, driven by scripts/kconfig.py).
#
#   $(O)/.config                       the configuration
#   $(O)/auto.conf                     Make fragment (CONFIG_FOO=..., strings unquoted)
#   $(O)/auto.conf.d                   Make deps: every Kconfig file that was read
#   $(O)/include/generated/autoconf.h  C header (#define CONFIG_FOO ...)
#
# auto.conf is regenerated whenever .config or any Kconfig file is newer, and
# GNU make restarts itself after remaking an included makefile, so the build
# always sees a configuration consistent with the Kconfig tree.

KCONFIG_SCRIPT := $(SRCTREE)/scripts/kconfig.py
KCONFIG_ROOT   := $(SRCTREE)/Kconfig
KCONFIG_CONFIG := $(O)/.config
AUTOCONF_H     := $(O)/include/generated/autoconf.h
AUTOCONF_MK    := $(O)/auto.conf
PYTHON         ?= python3

kconfig-env := srctree=$(SRCTREE) KCONFIG_CONFIG=$(KCONFIG_CONFIG) \
	       PROJECT_NAME='$(PROJECT_NAME)' PROJECT_VERSION=$(PROJECT_VERSION)
kconfig = mkdir -p $(dir $(AUTOCONF_H)) && \
	  $(kconfig-env) $(PYTHON) $(KCONFIG_SCRIPT) --out $(O) $(1)

config-targets := %_defconfig defconfig menuconfig guiconfig olddefconfig \
		  savedefconfig alldefconfig listnewconfig
config-only := $(filter $(config-targets),$(MAKECMDGOALS))
ifneq ($(filter-out $(config-targets),$(MAKECMDGOALS)),)
config-only :=
endif

.PHONY: $(config-targets)

%_defconfig: $(SRCTREE)/configs/%_defconfig
	$(call cmd,DEFCONF,$<) $(call kconfig,defconfig $<)

defconfig:
	$(if $(KCONFIG_DEFCONFIG),,$(error Use 'make <name>_defconfig' or KCONFIG_DEFCONFIG=<file>))
	$(call cmd,DEFCONF,$(KCONFIG_DEFCONFIG)) $(call kconfig,defconfig $(KCONFIG_DEFCONFIG))

menuconfig guiconfig olddefconfig alldefconfig listnewconfig:
	$(q)$(call kconfig,$@)

savedefconfig:
	$(call cmd,SAVEDEF,$(O)/defconfig) $(call kconfig,savedefconfig $(O)/defconfig)

ifeq ($(config-only),)
# Regenerate auto.conf/autoconf.h from .config (and re-sync .config against
# the Kconfig tree, like Linux' syncconfig).
$(AUTOCONF_MK) $(AUTOCONF_H) &: $(KCONFIG_CONFIG) $(KCONFIG_SCRIPT)
	$(call cmd,SYNCCONF,$(KCONFIG_CONFIG)) $(call kconfig,sync)

-include $(AUTOCONF_MK).d
-include $(AUTOCONF_MK)
endif
