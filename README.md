# RISC-V Trusted Firmware

Machine-mode firmware for RISC-V, structured as a monitor that hosts
services: the SBI, and next to it the security services a platform needs
(trusted execution, confidential computing, attestation) - the role
Arm Trusted Firmware plays on Arm, on RISC-V.

Status: build system and boot skeleton. The tree builds two images (a
first-stage loader and the runtime monitor) for RV32 and RV64 with GCC or
clang/lld, and boots on QEMU `virt`.

## Quick start

```
pip install kconfiglib            # once
make qemu_virt_rv64_defconfig     # or qemu_virt_rv32_defconfig, qemu_virt_rv64_2stage_defconfig
make -j
make run                          # QEMU; Ctrl-A X to quit
```

`make LLVM=1` builds with clang and ld.lld; `make help` lists everything.
See [docs/build-system.md](docs/build-system.md) for the layout and how to
add a platform, driver, service or image.

## Layout

```
arch/riscv/   entry, trap handling, ISA/CSR headers, -march derivation
platform/     one directory per board (QEMU virt today)
images/       separately linked binaries: loader, monitor
services/     ecall dispatcher and services (SBI base extension today)
drivers/      driver table, console, serial
lib/          freestanding libc subset, compiler helpers, libutils, utils
include/      tree-wide headers
mk/ scripts/  build system
configs/      defconfigs
```

## License

BSD-3-Clause, see [LICENSE](LICENSE). What came from elsewhere keeps its
license: the checkpatch wrapper scripts are BSD-2-Clause and `.clang-format`
GPL-2.0.
