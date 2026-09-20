# SBI implementation

The monitor implements the RISC-V SBI specification v3.0 for the S-mode
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
| SUSP      | `SUSP` | suspend to RAM as an M-mode wait with all other harts stopped (`CONFIG_SBI_SUSP`, on for QEMU virt) |
| FWFT      | `FWFT` | misaligned exception delegation; landing pad, shadow stack, double trap, PTE A/D updating and pointer masking where the hart has them; lock flag |
| SSE       | `SSE`  | all ten functions; sources: the software injected local and global events |
| DBTR      | `DBTR` | all eight functions, on harts with Sdtrig; address / data match triggers (mcontrol, mcontrol6), chains included |
| MPXY      | `MPXY` | all eight functions; channels carry RPMI service groups (see below); no MSI / SSE indication, notifications are polled |
| PMU       | `PMU`  | counters 0-31 = mcycle/minstret/mhpmcounterN, 16 firmware counters per hart, `event_get_info`; no snapshot shared memory |
| DBCN      | `DBCN` | write, read, write_byte (`CONFIG_SBI_DBCN`) |
| Legacy    | `0x00`-`0x08` | all v0.1 calls (`CONFIG_SBI_LEGACY`) |

Not implemented yet: CPPC (it needs a platform backend,
which RPMI can now provide). They probe as absent. NACL and
STA are interfaces a hypervisor offers its guests, not M-mode firmware.

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
                          misaligned.c    misaligned load / store emulation
                          pmp.c     PMP programming
                          pmu.c     hardware and firmware counters
                          fwft.c    firmware features (medeleg / menvcfg controls)
                          sse.c     supervisor software events
                          dbtr.c    debug triggers (Sdtrig)
services/mpxy/            MPXY core (shared memory, channels, attributes)
                          and the RPMI message protocol for channels
drivers/rpmi/             RPMI client + shared memory transport
drivers/irqchip/          PLIC, APLIC, IMSIC: machine-level set-up, found in the device tree
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
and software interrupts to the timer and IPI cores, illegal instructions
and misaligned accesses to their emulators. A misaligned load or store
(integer or floating-point, 32-bit or compressed encoding) is redone byte by
byte with the privilege and translation of the trapping context, unless
S-mode has taken misaligned exceptions for itself through FWFT. Everything else, and whatever the emulator does not handle, is
redirected to S-mode (`trap_redirect()`, which also fills in the hypervisor
CSRs when the H extension is present). A trap taken in M-mode is fatal
unless the hart announced it (`trap_expected`): that is how optional CSRs
are probed (`csr_probe()`) and how unprivileged accesses report faults.

Before entering S-mode a hart delegates the usual exceptions and the S-mode
interrupts, opens the counters, sets up menvcfg (Sstc, Zicbo*, Svpbmt as
available) and programs the PMP from the memory region list
(`include/memregion.h`): the monitor's image, plus what the drivers
registered at probe time. Machine-only regions (the CLINT, the machine-level
APLIC and IMSIC files, a protected RPMI transport) are closed to S/U-mode;
the last entry opens everything else. A region takes one NAPOT entry, or an
OFF + TOR pair when its bounds do not allow that. `smode_range_ok()` is the
matching software check for addresses S-mode passes in.

With **Smepmp** M-mode is confined as well (`mseccfg.MML` and `MMWP`): its
regions become locked rules, R-X for the monitor's code and constants and RW
for the rest (the linker script aligns the split), devices it shares with
S-mode (console, reset) get shared rules, and everything else is out of its
reach, S-mode memory included. Where a call has to touch a buffer S-mode
named by physical address (DBCN, the MPXY and DBTR shared memories, SSE
attributes, PMU event info), `smode_access_begin()` / `_end()` open a
two-entry window on exactly that range; `unpriv_*()` needs none, it goes
through `mstatus.MPRV`. `mseccfg.RLB` stays set: the window
is a shared-region rule written at run time, which QEMU only accepts under
MML with rule locking bypassed.

### Interrupt controllers

The monitor takes no external interrupts, but only M-mode can put the
controllers in a state S-mode can use. The drivers find them in the device
tree (a driver's `probe()` receives it): a PLIC gets every context quiet; an
APLIC root domain delegates the sources named by `riscv,delegate` to its
child domain and, in MSI mode, programs the IMSIC addresses of both levels
(the S-level domain can only read them). The M-level IMSIC files carry the
monitor's IPIs when they exist (one enabled identity per file, claimed
through `mtopei`), in preference to the ACLINT MSWI: an IPI backend names
the interrupt it arrives on and a rating.
The device tree fix-up then disables the machine-level APLIC and IMSIC
nodes and invalidates the PLIC's M-mode contexts, so that the next stage
only sees what it may drive. With Smstateen all state enables are opened:
the monitor has no policy that would keep state from S-mode.

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

### Performance counters

Hardware counter indices are the CSR offsets (0 = cycle, 2 = instret,
3-31 = hpmcounterN; 1 does not exist), found by probing at boot. Which
events a programmable counter can count, and the mhpmevent value for each,
comes from the `riscv,pmu` device tree node (`riscv,event-to-mhpmcounters`,
`riscv,event-to-mhpmevent`, `riscv,raw-event-to-mhpmcounters`); without it
only cycles and instructions are offered, on their fixed counters. A counter
is handed out stopped (mcountinhibit); cycle and instret run freely while
nobody owns them. With Sscofpmf the overflow interrupt is delegated and the
mode-filter flags of `counter_config_matching` are honoured; M-mode is
always filtered out. Firmware counters count the events of the SBI
specification where they happen (`pmu_fw_event()`).

### Supervisor software events

An event is injected where a trap from S/U-mode returns: `sse_process()`,
the last thing `trap_handler()` does, rewrites the register file so that the
mret lands in the handler, after saving what completion has to put back
(sepc, the sstatus and hstatus bits, a6, a7). This composes with everything
else at that point, a redirected exception included: the handler then
"interrupts" the first instruction of S-mode's trap vector. Another hart is
made to pass through there with an IPI, and a suspended hart wakes up for a
pending event. A per-hart flag says whether there may be anything to do, so
the common trap exit takes no lock. `sbi_sse_complete` rebuilds the
interrupted context in the register file, a0 and a1 included, so the
dispatcher writes no return value for it (`service_ret.keep_regs`).

Events come from a table (`local_ids`, `global_ids` in `sse.c`); a new
source adds its id and calls the injection path. Today those are the
software injected local and global events. The standard events without a
source here (RAS, double trap, PMU overflow) are `SBI_ERR_NOT_SUPPORTED`,
reserved ids `SBI_ERR_INVALID_PARAM`.

### Debug triggers

The Sdtrig CSRs are M-mode only; DBTR programs them on S-mode's behalf.
Each hart finds its triggers when it is started (tselect, tinfo). A trigger
index is the hardware trigger's index, so a chain gets contiguous indices
by getting contiguous hardware triggers. Configurations must have dmode and
m clear, and are taken all or nothing: validated and placed first, then
programmed, with a read-back that turns a WARL refusal into
`SBI_ERR_NOT_SUPPORTED`. "Off" is the trigger's type with nothing else set,
since tdata1 may refuse to read as zero (QEMU). A trigger that fires is a
breakpoint exception, which is delegated: the monitor is not involved.

### Firmware features

FWFT features are per hart and start from their reset value whenever a hart
is started (not when it resumes). Misaligned exception delegation toggles
medeleg; the others are menvcfg fields, and a feature exists when its field
can be written, so no extension list is needed. A pointer masking length
the hart does not implement is `INVALID_PARAM`.

### Message proxy (MPXY) and RPMI

```
S-mode driver --SBI MPXY--> services/sbi/mpxy.c      decoding
                            services/mpxy/mpxy.c      per-hart shared memory, channel
                                                      list, standard attributes
                            services/mpxy/mpxy_rpmi.c one channel = one RPMI service group
                            drivers/rpmi/rpmi.c       request / acknowledgment engine
                            drivers/rpmi/rpmi_shmem.c four queues + doorbell --> PuC
```

**MPXY core.** A channel is a `struct mpxy_channel` with the standard
attributes and ops for what its message protocol defines: protocol
attributes, sending a message, fetching notification events. S-mode's shared
memory page is validated like any address S-mode names, and is never trusted:
values are read once, and standard attribute writes are validated as a whole
before any of them is applied. The extension probes as present once a
channel exists.

**RPMI client** (`include/rpmi.h`). One transport, one request in flight:
the requester holds the lock from the enqueue until the acknowledgment
with its token arrives or `CONFIG_RPMI_TIMEOUT_US` expires, and polls for it
(serving its own IPIs meanwhile, like every M-mode wait). Acknowledgments
with another token are leftovers and dropped. `rpmi_poll()` drains the P2A
request queue: notifications go to the event sink of their service group,
requests from the PuC are answered `RPMI_ERR_NOT_SUPPORTED`. The shared
memory transport follows the specification's queue layout, takes its
geometry from Kconfig, and checks every index and length it reads, since the
other side is not this firmware. The queues can be hidden from S-mode with a
PMP entry (`RPMI_SHMEM_PROTECT`) and, when they live in RAM, are added to
`/reserved-memory` (`RPMI_SHMEM_IN_RAM`).

**RPMI over MPXY.** The platform binds channel ids to service groups with
`mpxy_rpmi_channel_add()`; BASE, CPPC and the M-mode only groups (system
reset, system suspend, HSM) are refused, as the RPMI specification demands.
`message_id` is the RPMI service id, the message data is the RPMI request
or acknowledgment data, so the service's own verdict is the STATUS word
while `sbiret.error` reports the proxying (`SBI_ERR_TIMEOUT`, `SBI_ERR_IO`).
The group version and the PuC's implementation id and version (the RPMI
channel attributes) are asked through the BASE group on first use, then
cached; a group the PuC does not implement makes its channel
`SBI_ERR_NOT_SUPPORTED`. Notification events are buffered per channel with
the events state (returned / remaining / lost).

## Testing

`images/sbitest` (`CONFIG_IMAGE_SBITEST`, on in the defconfigs) is an S-mode
payload that runs as the next stage and checks every call above: results,
error codes and side effects (pending and delivered interrupts, hart states
across start / stop / both suspend types / restart, IPI accounting on every
hart, trap redirection, the PMP fence around the monitor, legacy return
convention and unprivileged hart-mask reads). QEMU virt has no platform
microcontroller, so for MPXY and RPMI one of the secondary harts serves a
PuC model (`images/sbitest/puc.c`: BASE and clock service groups, plus test
services that stay silent, send a stale acknowledgment or fire
notifications) over the real shared memory queues, set aside in RAM by
`CONFIG_QEMU_VIRT_RPMI`. It prints `sbitest: PASS` or
`sbitest: FAIL` and powers off through SRST; `scripts/boot-test.sh` greps
for the verdict.

```
make qemu_virt_rv64_defconfig && make -j && make run
sh scripts/boot-test.sh build
QEMU_EXTRA_ARGS="-cpu rv64,sstc=off" sh scripts/boot-test.sh build   # M-mode timer path
make run QEMU_SMP=8
```

## Booting Linux

```
make qemu_virt_rv64_defconfig && make -j
make run QEMU_KERNEL=/path/to/Image \
         QEMU_ARGS="-append 'console=ttyS0 earlycon=sbi' -initrd rootfs.cpio.gz"
```

`QEMU_KERNEL` takes the place of the test payload as the next stage. Before
the hand-over the monitor adds its own memory to `/reserved-memory` in the
device tree (`no-map`, `images/monitor/fdt_fixup.c`); without that entry a
kernel allocates those pages and faults on the PMP fence. The tree is
grown in place, or moved to `CONFIG_MONITOR_FDT_ADDR` first. Tested with
Linux 6.19 and 7.3-rc on 4 harts, with and without Sstc, with the PLIC and
with `QEMU_MACHINE=virt,aia=aplic` (with `aia=aplic-imsic` the kernel comes
up on the IMSIC and the APLIC in MSI mode, but QEMU 8.2 then storms on
level-triggered sources, whatever the firmware): SMP bring-up, CPU
hotplug (HSM stop/start), `reboot` and `poweroff` (SRST), `earlycon=sbi`
(DBCN), the `riscv-pmu-sbi` driver finding its counters, and on 7.3-rc FWFT
and suspend to RAM (`echo mem > /sys/power/state`: CPUs offlined, SUSP,
resume, CPUs back).

Any other payload: `make run QEMU_ARGS="-device loader,file=payload.bin,addr=<MONITOR_NEXT_STAGE_ADDR>"`
with `IMAGE_SBITEST` disabled.

## Gaps

1. SSE event sources (PMU overflow, double trap, RAS); PMU counter
   snapshots; DBTR trigger types other than address / data match.
2. **RPMI consumers in M-mode**: system reset, system suspend, HSM and
   CPPC backends over RPMI; service groups implemented by the firmware
   itself behind the same MPXY channels; MSI / SSE indication of
   notifications; transport and channel discovery from the device tree
   (`riscv,rpmi-shmem-mbox`, `riscv,rpmi-mpxy-*`).
3. Emulation beyond `time` and misaligned scalar accesses: Zcb and vector
   misaligned accesses, atomics, missing counters.
4. **Device tree driven configuration.** libfdt is only used for the
   fix-up; device addresses and the hart count still come from Kconfig.
5. More timer / IPI / reset / serial drivers.
6. **Scalability.** Remote fences are serialised system-wide; harts are
   indexed by hart id (`hartid < CONFIG_PLATFORM_HART_COUNT`).

The H-extension paths (trap redirection from VS/VU-mode, `hfence` on a real
guest) are written after the specification but have not run under a
hypervisor yet.
