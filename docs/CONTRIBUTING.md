# Contributing

Contributions go through GitHub pull requests. The rules are the Linux
kernel's wherever the kernel has one.

## Before you start

* Look for an open issue or pull request on the subject. For a change of
  any size, open an issue first and say what you intend: it saves work on
  both sides when the design is discussed before the code.
* A bug is reported as a GitHub issue, with the platform, the
  configuration, the commit and what was observed. A suspected
  vulnerability is not: report it in private through GitHub's security
  advisory of the repository, or to a maintainer listed in
  [MAINTAINERS](https://github.com/riscv-trusted-firmware/riscv-trusted-firmware/blob/main/MAINTAINERS), as [threat-model.md](threat-model.md)
  says.

## Commits

* **One logical change per commit.** A commit does one thing and says
  why. A series of small commits is easier to review and to bisect than a
  large one; a rename or a move is a commit of its own.
* **Every commit builds and passes the tests** on its own, not only the
  last one of the series: `make`, `make W=1` (the CI builds with it, and
  every warning is an error), `sh scripts/boot-test.sh build`.
* **Every commit passes checkpatch**: `make checkpatch` on the working
  tree, `scripts/checkpatch.sh <base>..` on the series. The CI runs it on
  each commit of a pull request. [coding-style.md](coding-style.md)
  says what checkpatch does not.
* **The subject line** names the area first, then what the commit does, in
  the imperative: `drivers/irqchip: take the APLIC's delegation from the
  tree`, `sse: a source for the RAS events`. At most 75 characters, no
  full stop.
* **The body** says what was wrong or missing, what the change does about
  it and why it does it that way, wrapped at 72 columns. The reader is
  someone finding the commit in `git log` in two years' time. A bug fix
  names the commit it fixes:

  ```
  Fixes: 1234567890ab ("hsm: the platform's suspend types, through the RPMI")
  ```

* **Sign-off.** Every commit carries a `Signed-off-by:` line with the
  author's real name and address (`git commit -s`). It certifies the
  [Developer Certificate of Origin](https://developercertificate.org/):
  that you wrote the change or have the right to submit it under the
  project's license, BSD-3-Clause. A commit without it is not merged.
  Other tags follow the kernel's meaning: `Co-developed-by:` (with the
  co-developer's sign-off), `Suggested-by:`, `Reported-by:`, `Tested-by:`,
  `Reviewed-by:`, `Acked-by:`.
* **Authorship** is the commit's `Author:`. The copyright notice in a file
  is collective, "The RISC-V Trusted Firmware contributors", so nothing
  is added to it; a file taken from elsewhere keeps the notice it came
  with.

## Pull requests

* **Branch from `main`**, rebase on it before submitting and whenever the
  maintainers ask; do not merge `main` into your branch. The history is
  linear: a pull request is merged by rebase, never with a merge commit.
* **The description** says what the series does as a whole and how it was
  tested (platform, configuration, QEMU command line, hardware). A pull
  request that is not ready to merge is a draft.
* **Review happens in the pull request.** Reviewers comment on the code
  and, when satisfied, give their tag in a comment: `Reviewed-by: Name
  <address>` (read and agreed with), `Acked-by:` (no objection, from the
  maintainer of the area), `Tested-by:` (run on something). A tag given on
  a version of the commit holds until that commit changes in substance.
* **Updates are force-pushed.** Rework the commits themselves (`git
  rebase -i`) rather than adding "fix review comments" commits: the series
  is what gets merged. Say in a comment what changed since the last
  version. Before the merge, add the tags you received to the commits
  they apply to, under your sign-off, and push once more.
* **The CI has to be green**: builds with GCC and clang, RV32 and RV64,
  the boot tests, and checkpatch on every commit.
* **A maintainer merges** when the series has the tags it needs: at least
  one `Reviewed-by:` or `Acked-by:` from someone other than the author,
  as soon as the project has more than one person to give it. A
  maintainer's own series waits for that review like any other.

## What is in scope

The monitor implements ratified RISC-V specifications: the SBI, the
privileged architecture and its ratified extensions, RPMI and the device
tree bindings that go with them. A draft specification is not implemented
until it is ratified; what is implemented is written against the text of
the specification, and the pull request says which version. Code is
written with the threat model in mind: every new path that takes
something from S-mode says what it trusts and checks what it does not
([threat-model.md](threat-model.md)).

A new platform or driver comes with a way to test it, in the CI where
QEMU can run it, and with a note in the pull request of what it was run
on where QEMU cannot.

## Where things are

* [README.md](https://github.com/riscv-trusted-firmware/riscv-trusted-firmware/blob/main/README.md): what the project is and how to build and run it.
* [build-system.md](build-system.md): the tree, and how to add a
  platform, a driver, a service or an image.
* [coding-style.md](coding-style.md): how the code is written.
* [threat-model.md](threat-model.md): what the monitor protects.
* [MAINTAINERS](https://github.com/riscv-trusted-firmware/riscv-trusted-firmware/blob/main/MAINTAINERS): who reviews what.
