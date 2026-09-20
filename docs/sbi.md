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
| CPPC      | `CPPC` | probe, read, read_hi, write; backend: the RPMI CPPC service group |
| SUSP      | `SUSP` | suspend to RAM as an M-mode wait with all other harts stopped (`CONFIG_SBI_SUSP`, on for QEMU virt) |
| FWFT      | `FWFT` | misaligned exception delegation; landing pad, shadow stack, double trap, PTE A/D updating and pointer masking where the hart has them; lock flag |
| SSE       | `SSE`  | all ten functions; sources: the software injected local and global events, PMU counter overflow (Sscofpmf), double trap (Ssdbltrp) |
| DBTR      | `DBTR` | all eight functions, on harts with Sdtrig; address / data match triggers (mcontrol, mcontrol6), chains included |
| MPXY      | `MPXY` | all eight functions; channels carry RPMI service groups (see below); no MSI / SSE indication, notifications are polled |
| PMU       | `PMU`  | counters 0-31 = mcycle/minstret/mhpmcounterN, 16 firmware counters per hart, `event_get_info`, counter snapshots |
| DBCN      | `DBCN` | write, read, write_byte (`CONFIG_SBI_DBCN`) |
| Legacy    | `0x00`-`0x08` | all v0.1 calls (`CONFIG_SBI_LEGACY`) |
| Domain control | `0x0A000000` + impl. id | firmware specific: enter, exit, start, stop domains ([domains.md](domains.md)) |

All extensions of SBI v3.0 that are M-mode firmware's to provide are there. NACL and
STA are interfaces a hypervisor offers its guests, not M-mode firmware.

With domains ([domains.md](domains.md)) the
calls go by the caller's domain: its harts, its memory, its permission to
reset and suspend.

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
                          illegal_insn.c  counter CSR and AMO emulation
                          misaligned.c, misaligned_vector.c  misaligned load / store emulation
                          pmp.c     PMP programming
                          pmu.c     hardware and firmware counters
                          fwft.c    firmware features (medeleg / menvcfg controls)
                          sse.c     supervisor software events
                          dbtr.c    debug triggers (Sdtrig)
services/mpxy/            MPXY core (shared memory, channels, attributes)
                          and the RPMI message protocol for channels
drivers/rpmi/             RPMI client + shared memory transport
drivers/reset/ suspend/   RPMI backends for system reset, system suspend,
  hsm/ cppc/              hart power and CPPC, next to the local ones
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
and misaligned accesses to their emulators. Illegal instructions that are
emulated: reads of the counter CSRs the hart lacks (`time` from the platform
timer, `cycle`, `instret` and `hpmcounterN` from the machine counters,
honouring scounteren for U-mode), and the AMO instructions on a hart that
only has LR/SC, as an LR/SC pair under `mstatus.MPRV`. A misaligned load or
store (integer or floating-point, 32-bit, compressed or Zcb encoding; vector
unit-stride, strided and indexed accesses with masks, segments,
whole-register and fault-only-first forms and any LMUL) is redone byte by
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

### The device tree

The tree the previous stage passes is how the monitor learns the machine:
the console (`/chosen/stdout-path`), the CLINT or ACLINT timer and software
interrupt devices with the hart behind each of their per-hart registers, the
reset devices (the SiFive test finisher, generic `syscon-poweroff` /
`syscon-reboot`), the interrupt controllers, the PMU event map, the timer
frequency, and the RPMI transports with everything that uses them. Early in
the boot the tree is made writable with room to grow (`fdt_prepare()`), so
that a platform can add what its previous stage leaves out
(`plat_fdt_prepare()`; QEMU virt adds the RPMI test setup this way, and the
drivers then find it like on real hardware). Before the hand-over
`fdt_fixup()` takes the monitor's share out: its memory and any registered
region that lies in RAM become `no-map` reservations, the nodes of
`mmode_only` drivers (the RPMI transport and its users) and the
machine-level interrupt controllers are disabled, and so are the harts the
monitor does not manage.

### Interrupt controllers

The monitor takes no external interrupts, but only M-mode can put the
controllers in a state S-mode can use. The drivers find them in the device
tree: a PLIC gets every context quiet; an
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

### Hart ids and hart indices

Hart ids are the hardware's and can be anything; `CONFIG_PLATFORM_HART_COUNT`
only says how many harts the monitor manages. The boot hart builds the hart
table (`<boot.h>`): itself first, then the enabled cpu nodes of the device
tree in order. A hart's index is its id's position there, and everything
inside the monitor goes by index: the stack `entry.S` hands out, `struct
hart`, every per-hart array, hart masks, IPI targets, the per-hart register
tables of the CLINT and IMSIC drivers. Ids are translated where they cross
the boundary: SBI hart masks and hart ids coming in, a6 of an SSE handler,
the SSE preferred hart and RPMI messages going out. A hart that is not in
the table parks itself, and its cpu node is disabled for the next stage.

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
specification where they happen (`pmu_fw_event()`). Counter snapshots use
a per-hart page: `counter_start` can take initial values from it,
`counter_stop` writes the values and, with Sscofpmf, the bitmap of counters
that wrapped around, all relative to the call's `counter_idx_base`. While
the SSE PMU overflow event is enabled on a hart, the overflow interrupt is
not delegated there: M-mode takes it and raises the event, and re-arms it
when the handler completes.

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

Events come from a table (`local_ids`, `global_ids` in `sse.c`); a source
in the monitor raises its event with `sse_raise_local()` and follows the
event's state through `event_source_update()`. Today: the software injected
local and global events, the PMU overflow event, and the double trap event.
Only the software events can be injected by S-mode. The RAS events have no
source here and are `SBI_ERR_NOT_SUPPORTED`, reserved ids
`SBI_ERR_INVALID_PARAM`.

**Double traps.** With Smdbltrp the trap entry clears `mstatus.MDT` once
the interrupted state is saved, since the monitor takes traps in M-mode on
purpose (CSR probing, unprivileged accesses). With Ssdbltrp turned on by
S-mode (FWFT `DOUBLE_TRAP`), a trap redirected to S-mode sets `sstatus.SDT`
as the hardware would, and an S-mode trap taken with SDT set, whether the
hart reports it (cause 16) or a redirection finds it, goes to the SSE double
trap event; without a handler for it the hart is halted. SSE injection and
completion save and restore SDT. None of this could be run: QEMU 8.2 has
neither extension.

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

**RPMI client** (`include/rpmi.h`). A context is one transport instance to
one PuC, a `riscv,rpmi-shmem-mbox` node with its queues named in `reg-names`;
whoever uses RPMI names its context and service group in `mboxes`. Per
context one request is in flight: the requester holds the lock from the enqueue until the acknowledgment
with its token arrives or `CONFIG_RPMI_TIMEOUT_US` expires, and polls for it
(serving its own IPIs meanwhile, like every M-mode wait). Acknowledgments
with another token are leftovers and dropped. `rpmi_poll()` drains the P2A
request queue: notifications go to the event sink of their service group,
requests from the PuC are answered `RPMI_ERR_NOT_SUPPORTED`. The shared
memory transport follows the specification's queue layout, takes its
geometry from the node, and checks every index and length it reads, since the
other side is not this firmware. The queues can be hidden from S-mode with a
PMP entry (`RPMI_SHMEM_PROTECT`) and, when they lie in RAM, are added to
`/reserved-memory`.

**RPMI over MPXY.** A channel is a `riscv,rpmi-mpxy-*` node: `mboxes` says
which transport and service group, `riscv,sbi-mpxy-channel-id` what S-mode
calls it. BASE, CPPC and the M-mode only groups (system
reset, system suspend, HSM) are refused, as the RPMI specification demands.
`message_id` is the RPMI service id, the message data is the RPMI request
or acknowledgment data, so the service's own verdict is the STATUS word
while `sbiret.error` reports the proxying (`SBI_ERR_TIMEOUT`, `SBI_ERR_IO`).
The group version and the PuC's implementation id and version (the RPMI
channel attributes) are asked through the BASE group on first use, then
cached; a group the PuC does not implement makes its channel
`SBI_ERR_NOT_SUPPORTED`. Notification events are buffered per channel with
the events state (returned / remaining / lost).

The monitor knows the groups of RPMI v1.0 that are S-mode's to use
(`services/mpxy/mpxy_rpmi_groups.c`): SYSTEM_MSI, VOLTAGE, CLOCK,
DEVICE_POWER, PERFORMANCE, MANAGEMENT_MODE, RAS_AGENT and REQUEST_FORWARD.
Of those only a service the group has, with request data as long as that
service's, reaches the PuC: anything else is `SBI_ERR_NOT_SUPPORTED` or
`SBI_ERR_INVALID_PARAM` for the caller. A group the monitor does not know
(experimental, 0x7C00 up, or implementation specific, 0x8000 up; any
compatible above, or `riscv-tf,rpmi-mpxy-group`) is passed on as it comes.

SYSTEM_MSI is the one group where a request can be the monitor's business,
and those it answers itself, with the status the PuC would have used. The
system MSIs that prefer M-mode (the PuC says which, when the channel is
first used) and the one the transport node names as its P2A doorbell
(`riscv,p2a-doorbell-sysmsi-index`) are denied to S-mode, whatever the
service. And `SET_MSI_TARGET` has the PuC write to an address of the
caller's choosing: it must be one the caller's domain can write itself,
which the monitor's memory and its interrupt files are not
(`RPMI_ERR_INVALID_ADDR`).

**RPMI in M-mode.** The service groups the specification keeps for M-mode
back the monitor's own services: SYSTEM_RESET is a reset backend (the best
rated one that supports a reset type is used, so the PuC comes before a
local reset device, and a posted reset gets a grace period before the hart
gives up and halts); SYSTEM_SUSPEND tells the PuC when S-mode suspends the
system, before the last hart waits for its wake-up interrupt; HSM tells it
when harts start and stop, with the monitor's entry point as the address a
powered-up hart comes back through; CPPC serves the SBI CPPC extension. The
PuC is asked when something is wanted, not at boot. One that does not
answer is taken for one that does not offer the service, at the cost of a
timeout each time; one that answers no is an error for the caller.

## Testing

`images/sbitest` (`CONFIG_IMAGE_SBITEST`, on in the defconfigs) is an S-mode
payload that runs as the next stage and checks every call above: results,
error codes and side effects (pending and delivered interrupts, hart states
across start / stop / both suspend types / restart, IPI accounting on every
hart, trap redirection, the PMP fence around the monitor, legacy return
convention and unprivileged hart-mask reads). QEMU virt has no platform
microcontroller: the platform adds the nodes of one to the device tree
(`CONFIG_QEMU_VIRT_RPMI`), and one of the secondary harts serves a
PuC model (`images/sbitest/puc.c`: BASE, clock and system MSI service
groups, a group of its own with services that stay silent, send a stale
acknowledgment or fire notifications, and the reset, HSM and CPPC groups as
the monitor's backends
use them; the run ends with a shutdown that reaches the model over RPMI)
over the real shared memory queues. It prints `sbitest: PASS` or
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

`QEMU_KERNEL` takes the place of the test payload as the next stage; QEMU
says where it put it in the hand-over block ([boot.md](boot.md)). Before
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

1. SSE RAS events (no source yet); DBTR trigger types other than address /
   data match.
2. **RPMI**: service groups implemented by the firmware itself behind the
   same MPXY channels; MSI / SSE indication of notifications and the P2A
   doorbell as an interrupt (the P2A queue is polled); CPPC fast channels
   and the PuC's HSM suspend types.
3. The maximum number of harts and domains and the monitor's size are
   build-time constants.
4. More timer / IPI / reset / serial drivers.
5. **Scalability.** Remote fences are serialised system-wide.

The H-extension paths (trap redirection from VS/VU-mode, `hfence` on a real
guest) are written after the specification but have not run under a
hypervisor yet.
