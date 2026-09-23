# Coding style

The code follows the Linux kernel's coding style and is checked with the
kernel's `checkpatch.pl`. What that leaves open is settled below.

## License headers

Every file starts with its SPDX line and the project's copyright notice,
in the comment style of the file:

```
// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */
```

The notice is collective: the contributors are the authors of the
commits, and `git log` records who wrote what. A file taken from
elsewhere keeps the notices it came with (`lib/libfdt`).

## checkpatch

`checkpatch.pl` is the kernel's and is not shipped here. Point `CHECKPATCH`
at a copy (a kernel tree's `scripts/checkpatch.pl`, with `spelling.txt`
and `const_structs.checkpatch` next to it), or have it on the path, and:

```
make checkpatch                  # the working tree and the staging area
scripts/checkpatch.sh HEAD~3..   # commits, as the CI checks them
```

`.checkpatch.conf` holds the options: strict, 80 columns, codespell, and
the checks this tree does without. `typedefs.checkpatch` names the typedefs of
this tree. `scripts/checkpatch_inc.sh` leaves out what is not this
project's to style: `lib/libfdt`, the compiler runtime in `lib/builtins`,
the linker script, `stdint.h` (a standard header is made of typedefs), and
`lib/libutils`, which has ways of its own (the `for_each` macros and the
`volatile` accesses that checkpatch objects to live there, and nowhere
else).

Every commit has to pass: CI runs `checkpatch.sh` on each commit of a pull
request, and on the tip of a push. A `CHECK` counts as much as an error.

## The rules checkpatch does not check

* **Every variable is initialised where it is declared.** A scalar with
  `0` (or `false`, `NULL`) unless another value is the one that is meant,
  a struct or an array with `{ }`. `va_list` is the exception, since
  `va_start()` is its initialisation.
* **Unsigned constants** are written with `U()`, `UL()` and `ULL()` from
  `<util.h>`, not with a suffix: `UL(1) << 63`, not `1UL << 63`.
* **No typedefs** for structs; a spinlock is an `unsigned long`.
* **Device registers and memory another agent writes** are read and
  written through `<io.h>` (`io_read32()`, `io_write32()`, ...), a
  variable another hart writes through `READ_ONCE()`, `WRITE_ONCE()` or
  `<atomic.h>`. `volatile` is not used.
* **Attributes** come from `<compiler.h>`: `__aligned()`, `__noreturn`,
  `__noinline`, `__maybe_unused`, `fallthrough`.
* **Iteration** over a linker table is `LINKER_TABLE_FOREACH()` from
  `<linker_table.h>`, over the bits of a bit string `bit_foreach()` from
  `<bitstring.h>`; a macro that reuses an argument is not written outside
  `lib/libutils`.
* **80 columns**, comments included. A string that does not fit is split.
* **Commit messages**: a subject of at most 75 characters, a body wrapped
  at 72, and a `Signed-off-by:` line (`git commit -s`).

## clang-format

`.clang-format` knows the layout rules and this tree's iteration macros,
and nothing else; it does not always agree with checkpatch
(continuation lines under an open parenthesis, lines that end with one):
checkpatch has the last word.
