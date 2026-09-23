#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The RISC-V Trusted Firmware contributors
"""The architecture figure of docs/architecture.md: what the tree has, box
by box; run with the output file as the argument."""
import sys

W = 680
X0, GAP = 24, 12
FONT = "font-family='-apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif'"
STYLES = {
    'pay':  ('#e6f4ec', '#4a9a6e', '#1f6b45'),
    'mon':  ('#eceaf9', '#6b63c8', '#3b34a3'),
    'hw':   ('#f0ede6', '#8a8578', '#4f4a42'),
    'plan': ('#fbfbfa', '#b3add9', '#6a64a8'),
}
out = []
y = 0

def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def box(x, yy, w, h, style, title, lines, dashed=False):
    fill, stroke, text = STYLES[style]
    dash = " stroke-dasharray='5 4'" if dashed else ''
    out.append("<rect x='%d' y='%d' width='%d' height='%d' rx='8' fill='%s' stroke='%s' stroke-width='1.2'%s/>" % (x, yy, w, h, fill, stroke, dash))
    ty = yy + 21
    out.append("<text x='%d' y='%d' text-anchor='middle' font-size='13' font-weight='600' fill='%s'>%s</text>" % (x + w / 2, ty, text, esc(title)))
    for l in lines:
        ty += 14
        out.append("<text x='%d' y='%d' text-anchor='middle' font-size='10' fill='%s'>%s</text>" % (x + w / 2, ty, text, esc(l)))

def group(yy, h, title, dashed=False):
    dash = " stroke-dasharray='5 4'" if dashed else ''
    out.append("<rect x='%d' y='%d' width='%d' height='%d' rx='10' fill='#fafaf7' stroke='#d8d8d2' stroke-width='1'%s/>" % (X0, yy, W - 2 * X0, h, dash))
    out.append("<text x='%d' y='%d' font-size='14' font-weight='600' fill='#222'>%s</text>" % (X0 + 16, yy + 24, esc(title)))

def row(yy, cols, style, h, dashed=False, x0=None, w=None):
    x0 = X0 + 16 if x0 is None else x0
    w = W - 2 * X0 - 32 if w is None else w
    n = len(cols)
    bw = (w - GAP * (n - 1)) / n
    for i, (t, ls) in enumerate(cols):
        box(x0 + i * (bw + GAP), yy, bw, h, style, t, ls, dashed)

def arrow(x, y1, y2):
    out.append("<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#555' stroke-width='1.2'/>" % (x, y1, x, y2))
    d = -1 if y2 < y1 else 1
    out.append("<polygon points='%d,%d %d,%d %d,%d' fill='#555'/>" % (x, y2, x - 4, y2 - 7 * d, x + 4, y2 - 7 * d))

# ---- next stages ---------------------------------------------------------
y = 16
group(y, 118, 'S-mode next stages, one per domain')
row(y + 38, [
    ('Root domain', ['Linux, a hypervisor and its', 'guests, or the test payload']),
    ('Other domains', ['own harts and memory; entered', 'by ecall, or harts of their own']),
    ('Between them', ['REQUEST_FORWARD channels,', 'the bridge, management mode']),
], 'pay', 64)
y += 118
# arrows down to the monitor
for i in range(3):
    cx = X0 + 16 + (W - 2 * X0 - 32 - 2 * GAP) / 3 * (i + 0.5) + GAP * i
    arrow(cx, y + 6, y + 40)
out.append("<text x='%d' y='%d' text-anchor='end' font-size='10' fill='#555'>ecall, traps, interrupts</text>" % (W - X0 - 16, y + 26))

# ---- monitor ---------------------------------------------------------------
y += 46
gy = y
GROUP_AT = len(out)
out.append("<text x='%d' y='%d' font-size='14' font-weight='600' fill='#222'>M-mode runtime monitor</text>" % (X0 + 16, y + 24))
y += 38
row(y, [('Trap entry and dispatch', ['ecalls to the service table by EID; interrupts; redirection to S-mode or a hypervisor;',
                                   'emulation of misaligned accesses, counter CSRs and atomics'])], 'mon', 60)
y += 62 + GAP
row(y, [
    ('SBI extensions', ['Base, TIME, IPI, RFENCE, HSM,', 'SRST, SUSP, CPPC, FWFT, PMU, SSE,', 'DBTR, DBCN, MPXY, legacy, vendor']),
    ('Domains', ['harts and memory partitioned by', 'the device tree; contexts; enter,', 'exit, entry policy; state per domain']),
    ('MPXY and RPMI', ['channels to the platform controller;', 'groups the monitor serves itself:', 'REQUEST_FORWARD, MM, the bridge']),
], 'mon', 76)
y += 78 + GAP
row(y, [
    ('Hart runtime', ['hart table, PMP with Smepmp,', 'HSM states, remote fences,', 'timer, heap, hand-over block']),
    ('Per-hart service state', ['PMU counters, SSE events, triggers,', 'FWFT settings, hypervisor CSRs:', 'switched with the domain']),
    ('Boot and device tree', ['reserves the monitor, hides its', 'devices, cuts a tree per domain,', 'starts each domain on its boot hart']),
], 'mon', 76)
y += 78 + GAP
row(y, [('Planned: trusted execution, confidential computing, attestation', ['hosted next to the SBI on the same trap entry, domains and channels, as their specifications are ratified'])], 'plan', 46, dashed=True)
y += 48 + GAP
# platform layer
py = y
out.append("<rect x='%d' y='%d' width='%d' height='%d' rx='8' fill='none' stroke='#8a8578' stroke-width='1' stroke-dasharray='5 4'/>" % (X0 + 16, py, W - 2 * X0 - 32, 120))
out.append("<text x='%d' y='%d' font-size='13' font-weight='600' fill='#222'>Platform layer: the device tree decides</text>" % (X0 + 32, py + 22))
row(py + 34, [
    ('Platforms', ['QEMU virt; generic,', 'no board in it']),
    ('Drivers', ['serial, timer, IPI, irqchip,', 'reset, suspend, HSM,', 'CPPC, GPIO, RPMI']),
    ('Libraries', ['libc subset, libutils,', 'libfdt, builtins']),
    ('ISA support', ['Sstc, Smepmp, Sscofpmf,', 'Smstateen, Sdtrig, AIA, H,', 'Zkr, Ssdbltrp, Smcntrpmf']),
], 'mon', 72, x0=X0 + 32, w=W - 2 * X0 - 64)
y = py + 120 + 16
out.insert(GROUP_AT, "<rect x='%d' y='%d' width='%d' height='%d' rx='10' fill='#fafaf7' stroke='#d8d8d2' stroke-width='1'/>" % (X0, gy, W - 2 * X0, y - gy))

# ---- hardware -------------------------------------------------------------
y += 26
group(y, 108, 'Hardware')
row(y + 38, [
    ('PMP, Smepmp', ['domains and the monitor', 'kept apart']),
    ('ACLINT, CLINT', ['machine timer,', 'IPIs']),
    ('PLIC, AIA', ['APLIC, IMSIC,', 'MSIs']),
    ('Platform controller', ['RPMI shared memory', 'and doorbells']),
], 'hw', 56)
y += 108

# ---- boot chain -----------------------------------------------------------
y += 26
group(y, 116, 'Boot chain')
bw = (W - 2 * X0 - 32 - 3 * 28) / 4
bx = X0 + 16
by = y + 38
box(bx, by, bw, 64, 'hw', 'Previous stage', ['boot ROM, U-Boot SPL,', 'EDK2, coreboot, QEMU'])
box(bx + bw + 28, by, bw, 64, 'mon', 'Loader', ['optional first image;', 'two-stage builds'])
box(bx + 2 * (bw + 28), by, bw, 64, 'mon', 'Monitor', ['M-mode resident;', 'position-independent'])
box(bx + 3 * (bw + 28), by, bw, 64, 'pay', 'Next stages', ['one per domain, where', 'the hand-over block says'])
for i in range(3):
    x1 = bx + (i + 1) * bw + i * 28 + 5
    x2 = x1 + 18
    out.append("<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#555' stroke-width='1.2'/>" % (x1, by + 32, x2, by + 32))
    out.append("<polygon points='%d,%d %d,%d %d,%d' fill='#555'/>" % (x2, by + 32, x2 - 7, by + 28, x2 - 7, by + 36))
y += 116

# ---- legend ---------------------------------------------------------------
y += 18
lx = X0 + 16
for style, label in (('pay', 'S-mode next stages'), ('mon', 'M-mode firmware'), ('plan', 'planned'), ('hw', 'hardware, earlier stages')):
    fill, stroke, _ = STYLES[style]
    out.append("<rect x='%d' y='%d' width='12' height='12' rx='2' fill='%s' stroke='%s'/>" % (lx, y, fill, stroke))
    out.append("<text x='%d' y='%d' font-size='11' fill='#444'>%s</text>" % (lx + 18, y + 10, label))
    lx += 18 + 7 * len(label) + 28
y += 30

svg = ["<svg width='100%%' viewBox='0 0 %d %d' role='img' xmlns='http://www.w3.org/2000/svg' %s>" % (W, y, FONT),
       "<title>RISC-V Trusted Firmware: the monitor between the hardware and the S-mode stages it hosts</title>",
       "<rect width='%d' height='%d' fill='white'/>" % (W, y)] + out + ["</svg>"]
open(sys.argv[1], 'w').write('\n'.join(svg) + '\n')
