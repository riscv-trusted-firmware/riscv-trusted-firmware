# Build system

GNU Make + Kconfig (kconfiglib). No generated makefiles, no recursion into
sub-makes: one `make` invocation reads every `sub.mk`, builds one dependency
graph and links every enabled image.

## Layout

```
Makefile              entry point; PROJECT_NAME / PROJECT_VERSION live here
Kconfig               top-level menu; sources platform first (see "Defaults")
mk/config.mk          Kconfig targets and auto.conf / autoconf.h regeneration
mk/toolchain.mk       GCC (CROSS_COMPILE=) or LLVM=1; cc-option / ld-option
mk/flags.mk           global CPPFLAGS / CFLAGS / ASFLAGS / LDFLAGS
mk/subdir.mk          sub.mk processing (sources, per-dir and per-file flags)
mk/image.mk           multi-image rules: compile, preprocess linker script, link
mk/verbose.mk         V=1 handling
scripts/kconfig.py    kconfiglib front-end (defconfig, menuconfig, sync, ...)
scripts/boot-test.sh  boots a build on the platform simulator and greps the log
arch/riscv/           entry, trap entry, CSR/ISA headers, isa.mk (-march/-mabi from Kconfig)
arch/riscv/runtime/   what hosting S-mode takes (harts, trap policy, PMP, HSM); monitor only
platform/<v>/<b>/     Kconfig.plat + Kconfig + plat.mk + sub.mk + sources
images/<name>/        Kconfig + image.mk + <name>.ld.S + sources
lib/<name>/           Kconfig + sub.mk + sources (libc, builtins, utils)
drivers/<class>/      Kconfig + sub.mk + sources
services/<name>/      Kconfig + sub.mk + sources (core dispatcher, sbi, ...)
include/              tree-wide headers
configs/              *_defconfig files
```

Directories under `lib/`, `drivers/`, `services/`, `images/` and
`platform/*/` are discovered by glob, both in Kconfig (`source "lib/*/Kconfig"`)
and in Make (`lib/sub.mk` lists every `lib/*/sub.mk`). Adding a component is
adding a directory; nothing central is edited.

## Everyday use

```
make qemu_virt_rv64_defconfig      # pick a configuration (configs/)
make -j                            # build; output in build/
make run                           # boot on QEMU (platform hook)
make menuconfig                    # needs kconfiglib: pip install kconfiglib
make savedefconfig                 # minimal defconfig -> build/defconfig
make O=out/foo ...                 # other build directory
make LLVM=1                        # clang + ld.lld (LLVM=/path/ or LLVM=-18 as in Linux)
make CROSS_COMPILE=riscv64-linux-gnu-
make V=1 / W=1 / dump / check-toolchain / clean / distclean / help
```

The toolchain choice (`LLVM=1`, `CROSS_COMPILE`) is a command-line property,
not stored in the build directory, exactly like the Linux kernel.

## Configuration flow

1. `scripts/kconfig.py` loads `Kconfig`, applies a defconfig or the existing
   `.config`, writes `$(O)/.config`, `$(O)/include/generated/autoconf.h`
   (`#define CONFIG_...`), `$(O)/auto.conf` (Make fragment, strings unquoted)
   and `$(O)/auto.conf.d` (Make dependency on every Kconfig file read).
2. `mk/config.mk` includes `auto.conf`; when `.config` or any Kconfig file is
   newer, the `sync` command reruns and GNU make restarts. `.config` is
   resolved against the current tree on every sync (like `syncconfig`).
3. Every C/asm compile gets `-include autoconf.h`, so `CONFIG_*` is visible
   in code, in assembly and in linker scripts. A configuration value change
   recompiles what depends on `autoconf.h` through the usual `-MMD`
   dependency files.

Symbol conventions:

| Prefix               | Meaning                                                       |
|----------------------|---------------------------------------------------------------|
| `PLAT_<X>`           | platform choice entry (`Kconfig.plat`)                        |
| `PLATFORM_*`         | values every platform provides (dir, name, hart count, ...)   |
| `RISCV_ISA_*`        | extensions the compiler may use; they become `-march`         |
| `RISCV_EXT_*`        | privileged/runtime extensions the firmware drives; no compiler impact |
| `IMAGE_<NAME>`       | image enable; `<NAME>_*` its options                          |
| `SERIAL_*`, `SBI_*`  | driver / service options, owned by the driver or service      |

Only ratified extensions get a `RISCV_ISA_*` / `RISCV_EXT_*` symbol.

### Defaults: who wins

Kconfig takes the first `default` whose condition holds, in file order.
`platform/*/*/Kconfig` is sourced before everything else, so a platform can
set defaults for symbols owned by drivers or images without those files
knowing about the platform:

```
# platform/qemu/virt/Kconfig
if PLAT_QEMU_VIRT
config SERIAL_UART8250_BASE
	default 0x10000000
config LOADER_NEXT_STAGE_ADDR
	default 0x80200000
endif
```

Hard requirements go through `select` in `Kconfig.plat`
(`select RISCV_ISA_A`, `select SERIAL_UART8250`).

## sub.mk

```
srcs-y            += foo.c bar.S       sources, relative to the directory
srcs-$(CONFIG_X)  += baz.c             Kconfig-gated source
subdirs-y         += dir               recurse
incdirs-y         += include           -I for this directory's objects only
global-incdirs-y  += include           -I for every object of the image
cflags-y          += -DX               this directory's C files
cflags-foo.c-y    += -DX               one file
asflags-y / asflags-bar.S-y            assembly
cppflags-y                             C, assembly and linker script
```

`$(call cc-option,-flag)` is available in sub.mk for compiler-specific
flags. Two sources with the same basename in one image (`trap.c` + `trap.S`)
are rejected at parse time. Objects go to
`$(O)/images/<image>/obj/<path>/<file>.o`; the same source compiled into two
images is compiled twice, once per image, with that image's flags.

## Images

`images/<name>/image.mk`:

```
images-$(CONFIG_IMAGE_MONITOR) += monitor
monitor-dirs     := arch/riscv arch/riscv/runtime lib drivers services \
                    platform/$(CONFIG_PLATFORM_DIR) images/monitor
monitor-ldscript := $(SRCTREE)/images/monitor/monitor.ld.S
monitor-cppflags := -DIMAGE_MONITOR      # also -cflags, -asflags, -ldflags
```

An image lists exactly the directories it links: the loader takes
`drivers/core drivers/serial` rather than all of `drivers/`, and the S-mode
test payload (`images/sbitest`) only `lib` and itself, with its own entry
code instead of `arch/riscv`.

The linker script is preprocessed (`-x assembler-with-cpp -D__LINKER__`), so
it uses `CONFIG_*`. `arch/riscv/include/arch/image.lds.h` holds the common
layout (text, rodata with the `.service_table` / `.driver_table` linker sets,
data, bss, per-hart stacks); an image only defines `IMAGE_BASE` and
`IMAGE_SIZE`. `--orphan-handling=warn` and `--gc-sections` are on; the script
asserts the image fits and that no dynamic relocations survive.

Outputs: `<name>.elf`, `<name>.bin`, `<name>.map`, `<name>.ld`, and
`<name>.dump` with `make dump`. `$(O)/include/generated/version.h` carries
`PROJECT_NAME`, `PROJECT_VERSION` (with `git describe`) and `BUILD_TARGET`.

## Platforms

```
platform/<vendor>/<board>/
  Kconfig.plat   config PLAT_<X>  bool "..."  + select lines
  Kconfig        if PLAT_<X> ... defaults for PLATFORM_* and other symbols ... endif
  plat.mk        optional Make hooks: define plat-run (used by 'make run'), QEMU_* vars
  sub.mk         srcs-y += plat.c
  include/       platform headers (on the include path)
```

`plat.c` implements `include/platform.h`: `plat_early_init()` (console) and
`plat_init()`.

## Drivers and services

A driver is a `DRIVER_DEFINE()` descriptor in the `.driver_table` linker
set, probed by `drivers_init()`. A service is a `SERVICE_DEFINE()`
descriptor in `.service_table` owning an ecall EID range; `service_ecall()`
routes S-mode ecalls (SBI convention: a7 = EID, a6 = FID) to it; an optional
`probe` hook tells whether the service is usable on this platform. Timer,
IPI and reset drivers register an ops structure with the core of their
class (`include/timer.h`, `ipi.h`, `reset.h`). Device tree matching for
drivers is the next layer (libfdt goes under `lib/`). The SBI implementation
is described in [sbi.md](sbi.md).

## Toolchains

Tested: GCC 13 (`riscv64-unknown-elf-`, multilib rv32/rv64) and clang 18 +
ld.lld, both for RV64 and RV32, both with `-Werror`. `-nostdinc` plus the
compiler's own include directory keeps the host/newlib headers out; the tree
provides `string.h`/`stdio.h` under `lib/libc`. libgcc is linked when the
toolchain has one for the target; otherwise `lib/builtins` supplies the
64-bit division helpers.

## Testing

`scripts/boot-test.sh <build dir> [expected]` boots the build with the
platform's `plat-run` hook and greps the serial log: for the verdict of the
SBI test payload (`sbitest: PASS`) when the build has `IMAGE_SBITEST`, for
the monitor's idle message otherwise. `QEMU_EXTRA_ARGS` adds simulator
options (`-cpu rv64,sstc=off`). `.github/workflows/build.yml` runs the
defconfig matrix with both toolchains and boot-tests each result, with and
without Sstc.

## lib/libutils

Headers only, for everybody: `compiler.h` (attributes and builtins),
`util.h` (`BIT()`, `SHIFT_U32()`, `GENMASK_UL()`, `ROUNDUP()`, `MIN()`,
`IS_ALIGNED()`, register pairs and fields, arithmetic that reports an
overflow; the bit and shift macros work in assembly too), `types_ext.h`
(`vaddr_t`, `paddr_t`: what kind of address a number is), `atomic.h`
(`atomic_load_ulong()`, `atomic_cas_u32()`, ...) and `bitstring.h` (bit
strings by bit number). New code uses these rather than open-coded
shifts, masks and rounding.
