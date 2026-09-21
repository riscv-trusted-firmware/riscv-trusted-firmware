# Threat model

What the monitor protects, from whom, and by what means; and what it does
not. Every protection named here is in the code, and says where. What is
listed under "Not protected" is not a to-do list in disguise: it is what
somebody who builds on the monitor has to know.

The monitor is the most privileged software on the machine after the boot
ROM. A flaw in it is a flaw in everything above it, so the rule for a
change is the one this document was written by: say what the new code
trusts, and check that nothing less trusted can reach it.

## What is protected

1. **The monitor itself**: its code, data, heap and stacks, and the devices
   it drives for its own purposes (machine timer, IPI device, machine-level
   interrupt files and APLIC domain, RPMI shared memory and doorbell).
   Integrity first; there are no secrets in it, but there are pointers.
2. **Each domain from the others** ([domains.md](domains.md)): memory, the
   harts it runs on, the state the monitor keeps for it, its MPXY channels.
3. **The machine as a whole from a domain that has no right to it**: reset,
   suspend, stopping and starting other domains.
4. **S-mode from U-mode and a hypervisor from its guests**, as far as the
   monitor is in between: it must not become the way around a policy the
   more privileged of the two has set.

## Who is trusted with what

| | trusted for |
|---|---|
| Hardware, boot ROM | everything |
| The stage before the monitor | everything: it loads the monitor, and hands it the device tree and the hand-over block. The monitor checks what it is given for being well-formed, not for being honest |
| The device tree | the partitioning itself. It says what the domains are, who may enter whom, which channel is whose. It is not read again once S-mode runs |
| A platform microcontroller (RPMI) | what it is asked to do: reset, suspend, hart power, MSIs. Its answers are bounds-checked as data, and it is believed |
| The root domain | everything but the monitor. It has all memory. A tree that means to confine an operating system puts it in a domain of its own |
| A domain | its own regions and harts, nothing else |
| U-mode, guests | nothing |

The attacker is S-mode or U-mode software, or a guest, in any domain:
it executes what it likes, controls every ecall argument, every register at
a trap, the contents of all memory it can write, at any time and from
several harts at once.

## The monitor's own memory

* PMP on every hart before the hart first leaves M-mode, again after a
  non-retentive resume: `hsm_hart_enter()` → `hart_runtime_init()` →
  `pmp_hart_init()`. A hart that is not in the hart table, or has no
  S-mode, never runs anything but the monitor.
* Without Smepmp: one entry without permissions over `CONFIG_MONITOR_SIZE`,
  and one each for the monitor's devices, ahead of the domain's rules.
* With Smepmp: `mseccfg.MML` and `MMWP`. The image is locked R-X (text,
  read-only data) and RW (the rest), so M-mode cannot execute what it can
  write, or anything of S-mode's; it reaches S-mode memory only through a
  window of two entries that is opened around an access and closed after
  it (`smode_access_begin()`), which refuses a range that is the monitor's.
  A device the monitor shares with S-mode (console, reset device) has a
  rule that is rewritten with the running domain: shared only with a
  domain whose regions give it the device.
* A hart without PMP is refused (`CONFIG_INSECURE_NO_PMP` to bring up a
  core). The PMP granularity is probed, and range tops are rounded up to it.
* The trap entry takes its stack and its hart pointer from `mscratch` and
  the hart structure, never from a register S-mode controls; `gp` is
  reloaded. `mscratch` is zero while M-mode runs, which is how a trap from
  M-mode is told, and such a trap is fatal unless the code asked for it
  (CSR probing, the unprivileged access). Interrupts are never enabled in
  M-mode: `mstatus.MIE` is not set anywhere.
* A panic stops every hart.
* Built with stack canaries (a constant one), zero-initialised locals,
  `-fno-strict-aliasing`, `-fwrapv`, `-Werror`, a non-executable stack, and
  position-independent with its relocations applied by itself.
* The tree handed to S-mode reserves the monitor (`no-map`) and disables
  the nodes of what is the monitor's alone. That is for the benefit of an
  operating system that would otherwise fault on them; PMP is what holds.

## Addresses and data from S-mode

* A physical address from S-mode is checked against the **calling
  domain's** regions with the permission the access needs, and against the
  monitor's regions: `smode_range_ok()`, `smode_range_readable()`,
  `smode_entry_ok()`, all through `domain_range_ok()`, which rejects a
  range that wraps or is empty. The high word of a two-word address has to
  be zero. This covers DBCN, the PMU snapshot and event info, SSE
  attributes, the DBTR and MPXY shared memories, MSI targets, and the entry
  points of HSM start, suspend and resume.
* Shared memory is read once. SSE and DBTR copy what they validate; an MPXY
  message is copied into a buffer of the monitor's before any channel sees
  it, and only the response goes back (`mpxy_send_message()`), so that a
  channel cannot be made to check one request and send another.
* Virtual addresses (legacy hart masks, the instruction and operands of an
  emulated instruction) are accessed with `mstatus.MPRV`, that is with the
  privilege, translation and PMP view of the code that trapped, one
  instruction at a time, and a fault goes back to where it belongs, with
  the guest's fault information when the code was a guest's.
* Indices, counts and lengths are bounds-checked before use; responses say
  how much was written and no more is.
* Nothing S-mode supplies becomes `mstatus.MPP` = M. Redirected traps,
  injected events and new contexts enter S-mode or U-mode by construction,
  and the flags an SSE handler may rewrite are limited to what S-mode could
  set in `sstatus` and `hstatus` itself.
* State enables: `mstateen0` opens the bits the ratified extensions define,
  by name. Custom state and bits defined later stay closed.

## Domains

* PMP is reprogrammed from the domain's regions at every change of domain,
  smallest region first, the address translation caches flushed, guest
  ones included. A domain that needs more entries than the hart has stops
  the boot.
* A context is everything S-mode owns of a hart: register file, supervisor
  CSRs including `sstateen`, floating-point and vector state, the timer,
  the hypervisor's and its guest's CSRs, the MPXY shared memory. A context
  that has not run starts from zero, not from what was there.
* What the monitor keeps per hart for S-mode (PMU counters and their
  inhibit and overflow state, SSE events, debug triggers, FWFT settings)
  is kept per domain and hart, and its hardware side is taken out and put
  back with the context.
* **Moving a hart takes leave.** A domain is entered by ecall only from
  the domains its node lists (`riscv,entry-allowed-from`); without the
  property, from none. The root domain cannot be entered. Leaving a domain
  that nobody entered goes by the same list and never to the root domain.
  The MPXY bridge moves harts on the authority of its own node.
* Hart masks and hart ids in SBI calls are filtered by domain, legacy calls
  included: a domain cannot start, stop, query, interrupt, fence or inject
  an event into a hart that is not its own.
* System reset and suspend, and starting or stopping another domain, take
  the permission in the domain's node.
* An MPXY channel with `riscv,domain` exists for that domain only. A
  forwarded request lives in the monitor's memory, never in the other
  domain's; a request that times out is taken out of the queue before the
  memory it lives in goes away.
* A domain's device tree has its own harts and devices, its own memory,
  and none of the partitioning.

## Privilege levels below S-mode

* Counter and `time` reads that the monitor emulates honour `scounteren`
  for U-mode, and for a guest `hcounteren` (as a virtual instruction
  exception) and `htimedelta`.
* `mstatus.TVM`, `TW` and `TSR` are never set, and nothing of `wfi`,
  `sret`, `satp` is emulated: the monitor has no part in S-mode's policy
  for U-mode beyond the above.
* Debug triggers are programmed for S-mode with `dmode` and the M-mode
  match bit refused, and only of types that have mode bits to keep them out
  of M-mode with.

## Availability

Bounded: the work per interrupt, remote fence ranges (64 pages, then a
full flush), the per-hart fence queue (a full queue is served while
waiting, so two harts fencing each other do not deadlock), retries of an
emulated atomic instruction, and the time the console lock is held.
S-mode can cause M-mode interrupts only one per ecall (IPI, timer) or
while it has asked for them (PMU overflow, RAS).

## Not protected

These are limits of the design as it stands.

1. **M-mode is one compartment.** `mseccfg.RLB` stays set, because the
   window on S-mode memory and the per-domain device rules are written at
   run time; the locked rules therefore hold against S-mode and against a
   stray M-mode jump, not against M-mode code that means to rewrite them.
   There is no control-flow integrity (Zicfilp, Zicfiss) in M-mode, no
   guard page between a hart's stack and the next or the heap, and the
   canary is a constant. A write primitive in M-mode is the machine.
2. **No verified or measured boot.** The monitor authenticates nothing: not
   itself, not the next stage, not a domain's image. It enters what is at
   the address it was given. Whoever needs a chain of trust has to build
   it in the stages before, and it ends at the monitor.
3. **The previous stage and the tree are believed.** A malformed tree is
   met with bounds and panics where that was found to matter (loop bounds
   taken from properties, phandle loops, regions that overlap the monitor);
   a malicious one defines the partitioning and needs no bug.
4. **Side channels.** No mitigation of speculative execution, no cache or
   predictor flushing between domains, and `time`, `cycle` and `instret`
   are readable. Domains that share a hart share its microarchitecture.
   Counters are per domain, which removes the direct channel only.
5. **Interrupt routing is shared between domains.** PLIC, APLIC and IMSIC
   are devices: which domain can program which context or file is a matter
   of the regions the tree gives it. The PLIC's machine-level contexts are
   hidden in the tree but not protected by PMP; nothing enables the machine
   external interrupt with a PLIC, so writing them does nothing today.
6. **The console is everybody's**: any domain writes to it and reads from
   it. Keep nothing on it that matters.
7. **A domain that is called can keep the hart.** A request through the
   bridge, like any enter, has no time limit: the caller's hart is in the
   callee until it comes back. That is the usual bargain with a trusted
   environment, and it is one-sided.
8. **`next-mode` U-mode is a convention.** U-mode ecalls are delegated to
   S-mode, and the first one lands in the stage's own code with S-mode
   privilege. It confines nothing.
9. **Hart differences.** Features, PMP and Smepmp among them, are probed
   on the boot hart and assumed for all. Resumable NMIs (Smrnmi) are not
   handled. On a machine where they matter, the platform has to see to it.
10. **Denial of service by a domain against itself, or by the root domain
    against anybody**, is not a concern of the monitor's.
11. **Test builds.** The shipped configurations build the test payload, and
    on QEMU virt that leaves the RPMI queues and the machine-level
    interrupt files writable by S-mode and adds SBI calls for the payload.
    The monitor says so in its boot log (`INSECURE:`). They are for QEMU.

## Not exercised

Written against the specifications and reviewed, but never run, for want of
something to run them on: the misaligned and atomic instruction emulation,
everything a guest is involved in (redirection to HS-mode, faults of
emulated guest accesses, the hypervisor's CSRs with a real guest), double
trap handling, Smcntrpmf and Smcdeleg, the RAS interrupts, the drivers QEMU
has no model for, a PMP grain above 4 bytes, and RV32 with the H extension.
A bug there is as likely as anywhere else, and less likely to have been
found.

## Reporting

A suspected vulnerability is reported to the maintainer in private, not in
a public issue, with what is needed to reproduce it.
