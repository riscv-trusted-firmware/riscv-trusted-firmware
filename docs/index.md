# RISC-V Trusted Firmware

Machine-mode firmware for RISC-V, structured as a monitor that hosts
services: the SBI, and next to it the security services a platform needs
(trusted execution, confidential computing, attestation). It plays on
RISC-V the role Arm Trusted Firmware plays on Arm.

It boots on QEMU `virt` (RV32 and RV64, GCC or clang), brings up every
hart and starts an S-mode next stage behind a PMP fence. It implements
SBI v3.0 (Base, TIME, IPI, RFENCE, HSM, SRST, SUSP, CPPC, FWFT, PMU, SSE,
DBTR, MPXY, DBCN and the legacy calls), emulates the time CSR and
redirects traps, and proxies the RPMI service groups of a platform
microcontroller to S-mode over MPXY channels. The machine can be
partitioned into domains described in the device tree, and harts can move
between them. An S-mode test payload checks all of it on every build;
Linux boots on it with SMP, CPU hotplug, suspend to RAM, reboot and
power-off.

The code is at
[github.com/riscv-trusted-firmware/riscv-trusted-firmware](https://github.com/riscv-trusted-firmware/riscv-trusted-firmware),
under the BSD-3-Clause license.

## Where to start

```
pip install kconfiglib            # once
make qemu_virt_rv64_defconfig     # or qemu_virt_rv32_defconfig, generic_rv64_defconfig, ...
make -j
make run                          # QEMU; Ctrl-A X to quit
```

The defconfigs build the SBI test payload as the next stage: `make run`
prints its report and powers off.

## The pages

* [Booting the monitor](boot.md): how it is started, and told where the
  next stage is.
* [Build system](build-system.md): the tree, the configuration flow, and
  how to add a platform, a driver, a service or an image.
* [SBI implementation](sbi.md): what is implemented, how the services,
  the runtime and the drivers fit together, how it is tested, and what is
  still missing.
* [Domains](domains.md): partitioning the machine, moving harts between
  domains, requests between domains.
* [Threat model](threat-model.md): what the monitor protects, from whom,
  and what it does not.
* [Contributing](CONTRIBUTING.md): how a change gets in.
* [Coding style](coding-style.md): how the code is written.
