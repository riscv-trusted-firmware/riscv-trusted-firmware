# SBI implementation

The monitor implements the RISC-V SBI specification v2.0 for the S-mode
software it starts. This page says what is there, how it is put together,
and what is still missing.

## Extensions

| Extension | EID | State |
|-----------|-----|-------|
| Base      | `0x10` | complete; `probe_extension` asks the owning service |
| TIME      | `TIME` | `set_timer`; stimecmp with Sstc, M-mode timer + STIP injection without |
| IPI       | `sPI`  | `send_ipi` |
| RFENCE    | `RFNC` | all seven calls; the `hfence` ones need the H extension (`NOT_SUPPORTED` otherwise) |
| HSM       | `HSM`  | start, stop, status, suspend (default retentive and non-retentive types) |
| SRST      | `SRST` | shutdown, cold and warm reboot through the reset driver |
| DBCN      | `DBCN` | write, read, write_byte (`CONFIG_SBI_DBCN`) |
| Legacy    | `0x00`-`0x08` | all v0.1 calls (`CONFIG_SBI_LEGACY`) |

Not implemented yet: PMU, SUSP (system suspend), CPPC, NACL, STA, SSE,
FWFT, DBTR, MPXY. They probe as absent.

An extension whose backend is missing (no timer, IPI or reset driver)
probes as absent too, and its calls return `SBI_ERR_NOT_SUPPORTED`.

## Structure

Every extension is a service (`SERVICE_DEFINE`, see `include/service.h`)
under `services/sbi/`, a thin front end: it decodes the arguments, turns SBI
hart masks into `struct hartmask` (`sbi_hartmask()`), and calls the layer
that does the work:

```
services/sbi/*.c          argument decoding, SBI error codes
arch/riscv/runtime/       hart.c    per-hart state, feature probing, S-mode entry
                          trap.c    trap policy: ecall, interrupts, redirection
                          hsm.c     hart state machine, wait/suspend loops
                          rfence.c  remote fence requests
                          unpriv.c  memory access as the trapping context (MPRV)
                          illegal_insn.c  time/timeh CSR emulation
                          pmp.c     PMP programming
drivers/timer/            timer core + ACLINT MTIMER
drivers/ipi/              IPI core (event multiplexing) + ACLINT MSWI
drivers/reset/            reset core + SiFive test finisher
```

### Harts and traps

Each hart has a `struct hart` (`<arch/hart.h>`). In M-mode `tp` points to it
and `mscratch` is 0; while S/U-mode runs, `mscratch` holds the pointer. The
trap entry uses that to tell where a trap came from and switches to the
hart's M-mode stack for traps from below, so the monitor never runs on a
stack S-mode controls.

Traps from S/U-mode: ecalls go to the service dispatcher, the M-mode timer
and software interrupts to the timer and IPI cores, illegal instructions to
the emulator. Everything else, and whatever the emulator does not handle, is
redirected to S-mode (`trap_redirect()`, which also fills in the hypervisor
CSRs when the H extension is present). A trap taken in M-mode is fatal
unless the hart announced it (`trap_expected`): that is how optional CSRs
are probed (`csr_probe()`) and how unprivileged accesses report faults.

Before entering S-mode a hart delegates the usual exceptions and the S-mode
interrupts, opens the counters, sets up menvcfg (Sstc, Zicbo*, Svpbmt as
available) and programs two PMP entries: the monitor's memory without
permissions, then everything else RWX. `smode_range_ok()` is the matching
software check for addresses S-mode passes in (start and resume addresses,
DBCN buffers).

### Hart state management

The boot hart (whichever wins the boot lottery) initialises the monitor,
waits until every hart that entered the image is known, and enters the next
stage at `CONFIG_MONITOR_NEXT_STAGE_ADDR` with `a0` = hart id, `a1` = device
tree. All other harts are `STOPPED`: they sit in `hsm_wait_loop()` until
`hart_start` makes them `START_PENDING` and kicks them. `hart_stop` and a
non-retentive suspend abandon the ecall: the M-mode stack is restarted and
the hart later enters S-mode afresh. The default non-retentive suspend is
emulated (WFI, then resume at the given address); a suspended hart wakes up
when an interrupt S-mode has enabled becomes pending, and keeps serving
remote fences without waking up.

### IPIs and remote fences

The IPI core multiplexes events (S-mode IPI, remote fence, halt) over the
one M-mode software interrupt through a per-hart atomic pending word. Remote
fences are synchronous: one request at a time, published under a lock,
acknowledged by each target in a shared hart mask. A hart waiting for the
lock, for a start, or in a suspend loop keeps processing its own IPIs, so
harts fencing each other cannot deadlock.

## Testing

`images/sbitest` (`CONFIG_IMAGE_SBITEST`, on in the defconfigs) is an S-mode
payload that runs as the next stage and checks every call above: results,
error codes and side effects (pending and delivered interrupts, hart states
across start / stop / both suspend types / restart, IPI accounting on every
hart, trap redirection, the PMP fence around the monitor, legacy return
convention and unprivileged hart-mask reads). It prints `sbitest: PASS` or
`sbitest: FAIL` and powers off through SRST; `scripts/boot-test.sh` greps
for the verdict.

```
make qemu_virt_rv64_defconfig && make -j && make run
sh scripts/boot-test.sh build
QEMU_EXTRA_ARGS="-cpu rv64,sstc=off" sh scripts/boot-test.sh build   # M-mode timer path
make run QEMU_SMP=8
```

To boot something else, disable `IMAGE_SBITEST` and load it at
`MONITOR_NEXT_STAGE_ADDR`, e.g.
`make run QEMU_ARGS="-device loader,file=payload.bin,addr=0x80200000"`.

## Gaps

In rough order of what a Linux boot needs next:

1. **Device tree.** No libfdt yet: device addresses come from Kconfig, and
   the device tree is passed through unmodified. Linux needs the monitor's
   memory as a `reserved-memory` `no-map` node (it faults on the PMP fence
   otherwise), so booting Linux waits for the FDT fix-ups.
2. **Misaligned load/store emulation** (redirected to S-mode today) and the
   other illegal-instruction emulations.
3. **PMU**, then **SUSP**, **FWFT** and the rest of the list above.
4. **Smepmp.** PMP is programmed without `mseccfg.MML`.
5. **Interrupt controller set-up** (PLIC/APLIC M-mode contexts), more
   timer / IPI / reset / serial drivers.
6. **Scalability.** Remote fences are serialised system-wide; harts are
   indexed by hart id (`hartid < CONFIG_PLATFORM_HART_COUNT`).

The H-extension paths (trap redirection from VS/VU-mode, `hfence` on a real
guest) are written after the specification but have not run under a
hypervisor yet.
