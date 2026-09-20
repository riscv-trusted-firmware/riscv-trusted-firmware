# Booting the monitor

## What the monitor expects

Every hart enters the image at its first byte, in M-mode, with

| Register | |
|----------|-|
| `a0` | hart id |
| `a1` | device tree (the monitor completes it, fixes it up and hands it on) |
| `a2` | a hand-over block, or anything else |

The image is position independent (`CONFIG_MONITOR_PIE`): it runs where it
was put. The harts draw the boot hart among themselves; the others wait
until the boot hart has set things up, and then for the next stage to start
them with `sbi_hart_start()`.

## The hand-over block

Where the stage after the monitor sits is often known only to whoever
loaded it. The previous stage says so with a block of `unsigned long`s in
`a2` (`include/handover.h`, `CONFIG_BOOT_HANDOVER`):

| Field | |
|-------|-|
| `magic` | `0x4942534f` |
| `version` | 2; `boot_hart` is there from version 2 on |
| `next_addr` | where the next stage is; 0: not said |
| `next_mode` | 1: S-mode, 0: U-mode |
| `options` | bit 0: a quiet boot, warnings and errors only |
| `boot_hart` | the hart that boots; all ones: whichever comes first |

This is no new format: it is the block boot loaders for RISC-V already
build for the M-mode firmware they start (U-Boot SPL, EDK2, coreboot, QEMU
with `-kernel`, which know it as the firmware's "dynamic information"),
so the monitor can be started by
any of them as it is. What the monitor makes of it:

* `a2` is looked at with care, in `entry.S`: null, misaligned, unreadable
  (the access traps, and the trap ends the look) or without the magic
  number, it is no block, and nothing is said.
* `next_addr` and `next_mode` take the place of
  `CONFIG_MONITOR_NEXT_STAGE_ADDR` and S-mode: for the next stage, for the
  root domain, and for the boot hart's domain when the device tree gives it
  no `next-addr` ([domains.md](domains.md)). M-mode (3) is not a mode the
  monitor starts anything in: S-mode is used, with a warning.
* With `boot_hart`, only that hart takes part in the draw. Every hart gets
  the same block, as every hart gets the same device tree.
* The two-stage build's loader passes the block it was given on to the
  monitor: what it says is about the stage after the monitor.

Without a block the monitor goes by its configuration, a next stage at
a fixed address; with more than one piece of S-mode software
to start, the device tree says where each one is (domains).

```
make run QEMU_KERNEL=/path/to/Image     # QEMU loads it and names the address in the block
```
