# Domains

A domain is a share of the machine for one piece of S-mode software: harts,
memory regions with S/U-mode permissions, a next stage, and the right (or
not) to reset and suspend the system. A hart runs one domain at a time, and
everything the monitor does for S-mode on that hart goes by that domain:

* **PMP.** After the monitor's own entries come the domain's regions,
  smallest first, with the domain's S/U-mode permissions. They are
  programmed again whenever the hart changes domain.
* **Addresses in SBI calls** (shared memory, buffers, start and resume
  addresses, event handlers) must lie in memory the domain can use that
  way: read-write, or executable for entry points.
* **Service state.** What the monitor keeps for S-mode per hart (PMU
  counters and snapshot memory, SSE events, debug triggers, FWFT settings
  and locks, the MPXY shared memory) it keeps per domain and hart: domains
  that share a hart neither see nor disturb what the other has there.
* **Harts.** HSM, IPI and RFENCE calls see the harts of the caller's domain
  and no others: a foreign hart id is `SBI_ERR_INVALID_PARAM`.
* **System reset and suspend** are `SBI_ERR_DENIED` without the domain's
  permission. A domain that suspends while other domains run is suspended
  as far as its harts go; the platform stays up.

`CONFIG_DOMAINS` (on by default). Without a domain configuration in the
device tree there is the root domain alone: every hart, all memory but the
monitor's, which is how the monitor behaves without the option too.

## Device tree

The binding is the domain binding proposed for standardisation, under
`riscv,` names, with a `riscv,domain` phandle in whatever belongs to a
domain.

```dts
chosen {
    riscv-domains {
        compatible = "riscv,domain,config";

        tmem: tmem {
            compatible = "riscv,domain,memregion";
            base = <0x0 0x8e000000>;
            order = <24>;                   /* 2^24 bytes */
        };
        trtc: trtc {
            compatible = "riscv,domain,memregion";
            base = <0x0 0x101000>;
            order = <12>;
            mmio;
            devices = <&rtc0>;              /* disabled for who cannot touch it */
        };

        tdomain: trusted-domain {
            compatible = "riscv,domain,instance";
            possible-harts = <&cpu0 &cpu1 &cpu2 &cpu3>;
            regions = <&tmem 0x38>, <&trtc 0x18>;
            boot-hart = <&cpu0>;
            next-addr = <0x0 0x8e000000>;
            next-mode = <1>;                /* 1: S-mode (default), 0: U-mode */
        };
        udomain: untrusted-domain {
            compatible = "riscv,domain,instance";
            possible-harts = <&cpu0 &cpu1 &cpu2 &cpu3>;
            root-regions-inheritance = "all";
            regions = <&tmem 0x0>, <&trtc 0x0>;
            system-reset-allowed;
            system-suspend-allowed;
        };
    };
};
cpus {
    cpu0: cpu@0 { riscv,domain = <&udomain>; ... };
    ...
};
```

* Region permissions: bit 3, 4, 5 = S/U-mode read, write, execute. The
  M-mode bits (0-2) are accepted and ignored: what is the monitor's alone
  is described by the monitor's drivers, not by the tree, and is out of
  every domain's reach ("m-only" inheritance, always). `"all"` inheritance
  adds the rest of the root domain, everything, for the domain's own
  regions to take away from. `order` is 3 to XLEN; a region takes one PMP
  entry, and the boot stops when a domain needs more entries than the hart
  has left. There is no other limit to the number of domains and regions:
  they are counted at boot and kept on the monitor's heap.
* `riscv,domain` in a cpu node assigns the hart at boot; it has to be one
  of the domain's `possible-harts`. Harts without it run the root domain.
* `boot-hart` defaults to the monitor's boot hart. At the end of the boot
  every domain whose boot hart is assigned to it is started there at
  `next-addr` with `a0` = hart id and `a1` = `next-arg1` (default: the
  device tree). The boot hart's domain gets `CONFIG_MONITOR_NEXT_STAGE_ADDR`
  when it names no `next-addr`. The domain's other harts wait to be started
  with `sbi_hart_start()`.
* Domains are numbered in the order of their nodes, from 1; 0 is the root
  domain.

The tree the next stage gets is the one of the boot hart's domain: cpu
nodes of harts it will never run are disabled, as are the `devices` of
memory regions it cannot read or write; regions in RAM it has no access to
become `no-map` reservations; the domain configuration and the
`riscv,domain` properties are removed. A domain that wants another tree
names it with `next-arg1`.

## Moving between domains

A hart can leave its domain for another one and come back, which is how a
domain without harts of its own (a trusted environment next to an operating
system) gets to run. The calls are a firmware specific SBI extension,
EID `0x0A000000 + CONFIG_SBI_IMPL_ID` (`CONFIG_SBI_FW_DOMAIN`), and work on
the calling hart:

| FID | Call | |
|-----|------|-|
| 0 | `domain_count()` | number of domains |
| 1 | `domain_self()` | the caller's domain |
| 2 | `domain_enter(domain, arg)` | run `domain` until it exits; returns the value of that exit |
| 3 | `domain_exit(value)` | back to the domain that entered; returns the `arg` of the next enter |
| 4 | `domain_start(domain)` | start a stopped domain on its boot hart |
| 5 | `domain_stop(domain)` | stop every hart that runs it, forget its contexts |
| 6 | `domain_state(domain)` | 1 while a hart runs it or is to come back to it |

`domain_enter()` is open to any domain the hart is a possible hart of.
The first time it boots the domain on this hart: at `next-addr` if the
hart is the domain's boot hart; otherwise the hart stops there, for the
domain to start with `sbi_hart_start()`. From then on enter
and exit are the two ends of a call: enter resumes the domain where its
last `domain_exit()` was, which returns `arg`; the exit's `value` is what
enter returns. `domain_exit()` without anyone to go back to goes on to the
domains that have not had the hart yet and then to the root domain, which
is boot-time chaining; `SBI_ERR_DENIED` when there is nowhere to
go. Starting and stopping a domain other than one's own takes
`system-reset-allowed`. A domain that is stopped loses its contexts: harts
that were visiting go back where they came from, their enter call failed,
and the next enter boots the domain again.

What is switched, per domain and hart: the register file; sstatus, sie, sip
(SSIP), stvec, sscratch, sepc, scause, stval, satp, scounteren, senvcfg; the
timer deadline (stimecmp, or the M-mode timer's); the floating-point
registers and fcsr; the vector registers and CSRs up to
the boot hart's VLEN, which is what a context has room for (a hart with
larger ones has them cleared instead: nothing leaks, nothing survives the
call); on a hart with the H extension the hypervisor CSRs (hstatus, hedeleg,
hideleg, hie, hvip, hcounteren, hgeie, htval, htinst, hgatp, htimedelta,
henvcfg, hstateen0, and the AIA's hvien, hvictl, hviprio) and the VS-level
ones of the guest it had set up (vsstatus to vsatp, vstimecmp, vsiselect),
with the guest address translation caches flushed on the way, since two
domains' VMIDs are the same numbers; the MPXY shared memory. All of the
floating-point and vector state is switched whether sstatus says it is in
use or not, so that a domain never finds another one's values. The address
translation caches are flushed.

While a hart is away, the domain it left sees it as started. An IPI for it
is kept and delivered when it is back; remote fences skip it (it flushes on
the way back anyway).

The state of the SBI services moves with the hart as well. The software
side is simply kept per domain and hart (`domain_hart_alloc()`, `this_domain_key()`);
the hardware side is taken out and put back by `hart_services_switch_out()`
and `_in()`:

* **PMU**: mcountinhibit, every counter (cycle and instret included) and
  event selector, and whose the counter overflow interrupt is. A domain
  counts what happens while it runs; the counters stand still for it while
  the hart is elsewhere, and a new context starts from zero.
* **DBTR**: the installed triggers are read out and cleared, and written
  back on return; no trigger of one domain can fire in another.
* **FWFT**: the menvcfg fields and the misaligned exception delegation the
  features stand for, and the lock bits.
* **SSE**: events, global ones included, are a domain's own. One that becomes
  due for a hart that is away is delivered when the hart is back.

A context that has not run before, or whose domain was stopped since, finds
all of it as a started hart does: nothing registered, every feature off.

Not switched, and so shared by the domains that share a hart: external
interrupt routing (the PLIC or APLIC contexts and the IMSIC files of the
hart, guest interrupt files included, are devices: what keeps domains apart
there is which of them a domain's regions let it reach).

## Requests between domains

Entering a domain runs it on the caller's hart. Domains with harts of their
own talk through MPXY instead: RPMI's REQUEST_FORWARD service group, which
the monitor serves itself (see "RPMI served by the monitor" in
[sbi.md](sbi.md)). A domain that offers a service owns a
`riscv,rpmi-mpxy-request-forward` channel and takes requests from it; a
domain that uses one owns a channel whose requests are forwarded there,
today `riscv,rpmi-mpxy-mm-domain` for management mode. MPXY channels
belong to the domain named by `riscv,domain` in their node and
are invisible to the others.

Domains that share a hart need no second one for it: a bridge node
(`riscv,rpmi-mpxy-reqfwd-bridge`, [sbi.md](sbi.md)) has the hart that
makes the request carry it over and bring the answer back.

```dts
untrusted-to-trusted-bridge {
    compatible = "riscv,rpmi-mpxy-reqfwd-bridge";
    riscv,domain = <&tdomain>;                 /* takes the requests */
    riscv,sbi-mpxy-channel-id = <0x2>;
    source0 {
        compatible = "riscv,rpmi-mpxy-reqfwd-mm";
        riscv,domain = <&udomain>;             /* makes them */
        riscv,sbi-mpxy-channel-id = <0x1>;
        riscv,mm-memregion = <&mmmem>;
    };
};
```

With harts of their own on both sides, the two channels can also be nodes
of their own:

```dts
reqfwd-tdom {
    compatible = "riscv,rpmi-mpxy-request-forward";
    riscv,sbi-mpxy-channel-id = <0x2000>;
    riscv,sbi-mpxy-msg-max-len = <256>;
    riscv,domain = <&tdomain>;
};
mm-udom {
    compatible = "riscv,rpmi-mpxy-mm-domain";
    riscv,sbi-mpxy-channel-id = <0x1000>;
    riscv,domain = <&udomain>;
    riscv,reqfwd-target = <&tdomain>;
    riscv,mm-memregion = <&mmmem>;       /* RW for both domains */
    riscv,sbi-mpxy-completion-timeout-us = <200000>;
};
```

## Testing

`CONFIG_QEMU_VIRT_DOMAINS` makes the QEMU virt platform add a configuration
to the device tree in which the test payload runs as "untrusted", next to
"trusted" (no harts, entered from the payload) and "island" (the last
hart). All three run parts of the payload's image. The tests cover memory
isolation in both directions, address checks in SBI calls, hart visibility,
reset permission, enter / exit with register, FPU, vector and timer state,
PMU / SSE / DBTR / FWFT state set up on both sides of a shared hart (each
finds its own again and nothing of the other's, in the hardware as well),
a second hart visiting (stopped, started by the domain, IPI while away),
stopping and starting a domain that runs, a second boot of a stopped
one, and management mode: a hart of the payload's goes over to "trusted"
and serves `MM_COMMUNICATE` requests of "untrusted" from a REQUEST_FORWARD
channel (told of them by MSI, a 24-byte message through a 20-byte channel),
one of which it answers too late on purpose.

```
make qemu_virt_rv64_defconfig && echo CONFIG_QEMU_VIRT_DOMAINS=y >> build/.config
make olddefconfig && make -j && make run QEMU_ARGS="-cpu rv64,v=true"
```

Linux boots inside a non-root domain as it does in the root domain: fewer
CPUs, the other domains' memory reserved.
