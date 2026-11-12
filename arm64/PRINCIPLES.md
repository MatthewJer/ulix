# Principles for the ARM64 / Raspberry Pi work

Ulix is a **teaching operating system**. The authors (Hans-Georg Eßer and Felix Freiling) wrote it as a literate program, so that
students can read it like a book and see how operating system concepts (paging, scheduling, file systems, drivers) are really
implemented. The port, the Raspberry Pi 4B work and everything after it keep that idea. When there is a choice, **being easy to
understand wins over being fast, complete or clever**.

(This file is not the work of the original authors; it records what this fork has decided to follow from their README and book.
See the notice in `README.md`.)

## What this means in practice

1. **Simple first, then better.** The book shows the simple version of a driver before the better one: the serial disk first with
   polling, then with interrupts and sleeping processes. Do the same: a polling driver that works comes before DMA, interrupts
   or speed-ups, and each step is small enough to read and test on its own.
2. **One idea per file or function, named like the hardware's own words.** Register names and steps follow the datasheet, so that
   a student can look them up. No framework, no layers of abstraction that only exist for portability.
3. **Comments explain why**, in plain language, in the style of the surrounding code. Say what the hardware expects and what goes
   wrong otherwise (for example: "qemu's PL011 has no receive timeout, so the FIFO is off").
4. **Reuse the book.** The kernel logic of `ulix-book.nw` stays the single source (`port.manifest`). Hardware-specific code is
   written again with the same names and data structures, and only for what the hardware forces. Changes to the book must keep the
   x86 build as it was, or fix a real bug (and then be listed in `README.md`).
5. **Same shape as the x86 version.** A process that reads sleeps in a blocked queue and the interrupt wakes it; the idle thread and
   the boot code poll. New drivers follow that pattern instead of inventing another one.
6. **Everything is tested, and the test says what it proves.** New code comes with a check in the kernel tests or a host-side
   test (like `make sdisk-test`), and `make pcheck` stays green.
7. **Literate programming: the chapters in `lit/` are the source.** The files of the port (`*.c`, `*.h`, `*.S` in `arm64/`,
   `user/`, `userland/`) are *generated* from `lit/*.nw` with `notangle` (`tools/tangle.sh`, run by `make`) and are not tracked by git.
   Change the chapter, not the generated file. Explanations go into the text between the chunks, so that the book of the ARM64 part
   (`make book`, `lit/build/ulix-arm64.pdf`) stays complete: every new file becomes a section of a chapter and is listed in
   `lit/FILES`. Comments inside functions stay short; the long explanation is text.
8. **Document as a log of reasoning**, not only of results: `ARM64-PORT.md` says why each step was done, and its last
   section says what is open. There is no separate TODO file: a list of open points that nobody reads is worse than none.

## Code from the BSD kernels (decision of 2026-11-03)

BSD drivers (FreeBSD, NetBSD, OpenBSD) are the model for the Raspberry Pi drivers. Never copy Linux code (GPL-2.0-only, this
project is GPLv3); reading Linux for facts such as register layouts is fine.

- Take **register definitions, initialisation sequences and the transfer logic**, not the framework around them (newbus,
  `bus_space`, mbuf, the mmc stack). The result must read like the rest of Ulix, not like a FreeBSD file with the glue cut out.
- Every adapted file **keeps its original copyright and licence text** and gets a line saying where it came from and what was
  changed. List it in `README.md` (third-party code).
- Check the licence header of each file first (BSD-2/3-Clause is fine).
- Where a BSD driver is much bigger than the job needs, write the small version the way the book would, and use the BSD file as
  the reference for facts.

## Third-party libraries (convention of 2026-11-12, issue #16)

Whole libraries (BearSSL, later musl, LVGL, ...) are not copied into the book and not into `arm64/`. They live **unmodified** as git
submodules in `third_party/<name>`, pinned to a release tag (`git submodule status` shows the tag). What has to be changed goes into our
own files next to the library (a configuration header, the glue to the system calls) or into patches in `patches/<name>/`; only *our*
files become a section of the book. The Makefile compiles a library file straight from `third_party/` (see the rules for BearSSL in
`userland/Makefile`). A library that is switched off at build time (`make TLS=0`) must not even be fetched or compiled.

Before a library is added, its entry is written to the table in `third_party/README.md`:

| field | content |
|---|---|
| name, upstream URL, tag | where it comes from, the tag the submodule is pinned to |
| licence | the licence text of the upstream `LICENSE` file, checked on the day of the import (BSD, MIT, ISC, Apache-2.0 are fine; never Linux code) |
| why it is compatible with GPLv3 | one sentence (for MIT/BSD: permissive, only the notice must stay) |
| what we add | our files, patches, configuration |
| what it costs | size of the code, RAM use, libc functions it needs |

## Plan for the Raspberry Pi 4B (branch `rpi4`)

Order decided on 2026-11-04: **first make Ulix run on the board, then networking, then telnet.** Goal at the end: Ulix boots on the
Pi and a user can log in over telnet.

1. **Boot and console on the board.** `kernel8.img` at `0x80000`, device tree from the firmware (`dt.c`), RAM from address 0,
   peripherals at `0xFE000000`, the PL011 (or mini UART) on GPIO 14/15, GIC-400, generic timer. Done in qemu; the checklist for the board is section 23 of `ARM64-PORT.md`.
2. **SD card** (SDHCI/Arasan, polling first) instead of virtio-blk; then CPUs, clock without RTC. Also done in qemu; see the same section.
3. **Networking** (first in qemu `virt` with virtio-net, then the GENET driver of the Pi): Ethernet, ARP, IPv4, ICMP, UDP, a small
   TCP, each layer small and testable. Written as a literate chapter in the style of the book if possible.
4. **Telnet server** that attaches a session to a virtual terminal (the book already has ten of them).

## Branches and builds

- Work happens on the branch `rpi4` (from `arm64-port`). It is merged back only when `make pcheck` is green with the Pi changes.
- **One source tree, two images.** `make` still builds the qemu image. `make kernel8.img` builds the image for the board: the same
  objects, only the boot code (`start-rpi4.o`: which GByte holds the console UART) and the load address (`0x80000`, set at link
  time) differ. Everything else, including the memory map, comes from the device tree at run time.
- Drivers are normal files in `arm64/`. The kernel picks them from the `compatible` strings of the device tree, as it already does
  for virtio and the PL011. No `#ifdef` maze: platform differences live in `plat.h`/`dt.c` and in the driver files.
- `origin` is the original project and is never pushed to; backups go to the private repository (`backup`).
