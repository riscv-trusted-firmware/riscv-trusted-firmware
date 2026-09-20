# RISC-V Trusted Firmware

Machine-mode firmware for RISC-V, structured as a monitor that hosts
services: the SBI, and next to it the security services a platform needs
(trusted execution, confidential computing, attestation) - the role
Arm Trusted Firmware plays on Arm, on RISC-V.

Status: the monitor boots on QEMU `virt` (RV32 and RV64, GCC or clang/lld),
brings up every hart and starts an S-mode next stage behind a PMP fence. It
implements SBI v2.0: Base, TIME, IPI, RFENCE, HSM, SRST, DBCN and the legacy
calls, with time CSR emulation and trap redirection; an S-mode test payload
checks all of it on every build. Device tree handling (needed to boot
Linux) and the PMU extension are next: see [docs/sbi.md](docs/sbi.md).

## Quick start

```
pip install kconfiglib            # once
make qemu_virt_rv64_defconfig     # or qemu_virt_rv32_defconfig, qemu_virt_rv64_2stage_defconfig
make -j
make run                          # QEMU; Ctrl-A X to quit
```

The defconfigs build the SBI test payload as the next stage: `make run`
prints its report and powers off. `sh scripts/boot-test.sh build` does the
same and checks the verdict.

`make LLVM=1` builds with clang and ld.lld; `make help` lists everything.
See [docs/build-system.md](docs/build-system.md) for the layout and how to
add a platform, driver, service or image.

## Layout

```
arch/riscv/   entry, trap entry, ISA/CSR headers, -march derivation;
              runtime/: per-hart state, trap policy, PMP, HSM, remote fences
platform/     one directory per board (QEMU virt today)
images/       separately linked binaries: loader, monitor, sbitest (S-mode)
services/     ecall dispatcher and services (the SBI extensions today)
drivers/      driver table, console, serial, timer, ipi, reset
lib/          freestanding libc subset, compiler helpers, libutils, utils
include/      tree-wide headers
mk/ scripts/  build system
configs/      defconfigs
```

## License

BSD-3-Clause, see [LICENSE](LICENSE). What came from elsewhere keeps its
license: the checkpatch wrapper scripts are BSD-2-Clause and `.clang-format`
GPL-2.0.
