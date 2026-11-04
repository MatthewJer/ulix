# ARM64 port: open points

State: the port runs in `qemu-system-aarch64 -M virt` and passes `make check` (see `README.md`). This list is what
is still missing compared with the x86 build, most important first. Background and reasons: `../ARM64-PORT.md`
(sections 14-19).

## Where we stopped (2026-11-03) -- start here

All tests are green: `make pcheck` (all 21 tests in parallel, about 3 minutes, `JOBS=n` sets the number of parallel qemu
instances, logs in `check-logs/`) and the older sequential `make check` (about 30 minutes).
The branch `arm64-port` is backed up in the PRIVATE repository https://github.com/MatthewJer/ulix-arm64 (remote `backup`;
`origin` is the original, `hgesser/ulix`: never push there).

### Done on 2026-11-03

- The hang in the memory test: `fork ()` brings back all swapped-out pages of the parent; with full RAM each page_in evicted
  another page of the same parent. Fixed with `arch_evict_protect (old_as)` around `clone_level` in `u_fork`.
- The swap file is cut back when no page lives in it (`arch_swap_shrink`). The book's `mx_ftruncate` is unfinished: it frees
  the data zones in the bitmap but keeps the zone pointers and indirect blocks in the inode (the code for the indirect
  blocks is empty). When the file grew again, old and new pointers met ("zone N used by /tmp/swap and /tmp/swap" in
  `tools/minix_check.py`). The same bug is in the x86 build for `trunc`/`ftruncate` (original code: [mx_ftruncate](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23825-L23866), see "Bugs of the book that are not fixed" in `README.md`). The swap code now deletes and recreates
  the file instead; the book's function is not changed.
- The memory test (`proc_test.c`) calls `arch_threads_reap ()` between its two rounds (a process does this at `fork`).
- System call 777 (`inode`, the book's debug call) was not installed: added (also to `port.manifest`). `readelf` has an
  ELF64 version (`userland/readelf64.c`). `inode`, `su`/`chown` and `readelf` are in the shell test.
- Run by hand and fine: `fork2`, `sbrk`, `paging`, `tp`, `bindump`, `su`, `chown`, `chgrp`. Not run: `testcp` (loops, needs
  /mnt), `xlogin`, `init2`, `fbomb`.
- Second disk: `/dev/hdb` (a second virtio-blk device, 32 MByte, `make disk2.img` with `tools/minix_mkfs.py`) is mounted on
  `/tmp/` like on the x86 and holds the swap file; without a second disk everything is as before. `/dev/` is now mounted
  (it was a plain directory: `/dev/kmem`, `/dev/hda` and `/dev/hdb` could not be opened). The disks are numbered from
  the highest MMIO slot down, because qemu fills the slots from the end. `make pcheck` has two jobs with two disks.
- A process that waited for the disk ran only at the next timer tick (10 ms) after the interrupt woke it; with much
  swapping this made the memory test take minutes. The disk interrupt now calls the scheduler at once when the CPU is idle.
- Third disk (`make disk3.img`, 1440 KByte, `a.txt` in it) stands in for the floppy `/dev/fd1` and is mounted on `/mnt/` like
  on the x86; `testcp` runs (the shell test ends it with Ctrl-C and compares `b.txt` with `a.txt`).
- Status line: the book's `_set_statusline` text is painted on line 25 of the console (`arch_vt.c`, at every new line).
  The test scripts cut that escape sequence out of the output they check.
- `xlogin`, `init2` and `fbomb` were run and work (`fbomb` is in the shell test); `readelf`, `inode`, `su`, `chown` too.
- Two checks of `user/sigtest.c` assumed one CPU (after SIGKILL the first `waitpid` may still return the child, after SIGSTOP a
  child on another CPU stops a moment later); with the faster disk wakeup they failed about every second run with 2 CPUs.
- Open, same bugs as on the x86: the book's `mx_ftruncate` leaves zone pointers and indirect blocks in the inode, and
  `unlink` of a file with double indirect zones (more than 263 KByte) leaves zones marked wrongly (original code of the second
  one: [free this inode: (3) double indirect blocks](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23705-L23729)). The swap file is
  therefore never cut back when it lives on the second disk. Locations of all bugs of the book: `README.md`.
- `make verify` and `pcheck` now fail when the file system check finds problems (before, they only printed them).

### Results of the "does it work on the ARM64 like on the x86?" check so far

- All 61 system calls of the book are installed (8 have ARM64-specific handlers: threads, mutexes, idle). OK.
- All 23 kernel shell commands of the book (and `exit`) work (see above). OK.
- Programs of `lib-build/tools` that were run and behave like on the x86: args, ascii, cat, cp, diff, grep, wc, hexdump,
  touch, mkdir, ls, stat, chmod, rm, rmdir, df, ptree, pw, kill, sync, exit42, stdio, fork1, fork3, sbrk1, trunc, tt,
  print, memwrite (with the screen window), fault-mem, fault-div0, mutex, dumblock (same "cannot remove process ...
  probably failure" line as the book's code), filecomm, keys, swapper ("don't start me manually"), clear, ps, sigtest
  (after the user_to_phys fix), thread, recurse, vi, login, sh.
- Differences that remain (cannot or should not be changed):
  - `df` lists /dev/hdb and /dev/fd1 with the numbers of /dev/hda ("none" mount); check what the x86 shows without those
    disks (look at `df.c` and `syscall_diskfree`).
  - `/dev/fd0` has no drive behind it (there is one stand-in for the second floppy only).
  - The swap file grows on demand instead of being 64 MByte in advance (disk is 4 MByte).

### What the ARM64 version can do that the x86 version cannot (for the README / book appendix)

More than one CPU (1 to 8, with a big kernel lock), device tree and GICv2/GICv3, more than 64 MByte of RAM (tested up to
2 GByte), 64-bit pointers, stack growth up to 64 MByte, interrupt-driven disk like the book's IDE driver, swap that does
not hang the kernel when it is full, division by zero and out-of-memory handled without stopping the kernel. Still to
verify and list properly tomorrow.

### Still missing compared with the x86 (decided: do these later, they are the biggest items)

- ~~**Serial disk**~~ **done on 2026-11-04**: `readblock_serial` / `writeblock_serial` in `glue.c` talk the book's protocol (CMD_GET / CMD_PUT,
  block number as 4 bytes, 1024 bytes of data) over a second PL011 (`plat.uart2`, the second `pl011` node of the device tree; qemu
  creates it when it gets a second `-serial`). A reading process sleeps in `serial_disk_queue` until the receive interrupt has collected
  the block; the idle thread polls. The other end is `tools/serial_disk.py` (serves an image file over qemu's TCP serial port).
  `make sdisk-test` (also part of `make pcheck`) runs the kernel tests with it: block I/O, the Minix file system of the book on
  device 0x2a00 (`mx_stat`, `mx_read`, `mx_open`/`mx_write`), the buffer cache, two threads reading at the same time (sleep and
  wake-up through the interrupt), and afterwards checks the image on the host with `minix_check.py`. Nothing in the book uses
  `/dev/sdisk` (its table of devices says the device file does not exist), so it is not mounted and has no device file here either.
  qemu's PL011 has no receive timeout, so the port runs with the FIFOs off (one interrupt per byte); the baud rate is not set
  (qemu ignores it, a real UART would need it).
- **`/dev/fd0`** has no drive behind it: a fourth virtio disk would do (small).
- Not checkable here: the x86 build with `gcc-4.4` (the compiler is not available on this machine).
- Bugs of the book that are on the x86 as well and were not changed: `mx_ftruncate`, `unlink` of files with double indirect
  zones (see "Done on 2026-11-03"), `u_write` returning -1, `mx_mkdir` without an existence check, `u_kill` on a blocked parent
  (list with the places in the original book: "Bugs of the book that are *not* fixed" in `README.md`).

### Next: Raspberry Pi 4B (hardware ordered 2026-11-03: Pi 4B 1 GB, USB-C power supply, Waveshare CP2102 USB-C UART module,
### 8 GB microSD with card reader, Dupont cables)

Why the 4B: BCM2711 has a real GIC-400 (GICv2: `gic.c` works), Ethernet (GENET) without USB, a UART header that is soldered.
Plan, in this order; each step is first tried in qemu (`-M raspi4b`, if the installed qemu has it; else `raspi3b` for the
parts that are the same) and then on the board over the serial adapter (3.3 V! TX/RX crossed: adapter TXD to pin 10, RXD
to pin 8, GND to pin 6; never connect 5 V):

1. Boot: the firmware loads `kernel8.img` at 0x80000 (our Linux-arm64 image header fits) and hands over a device tree
   (`dt.c` reads it). RAM starts at 0, not at 0x40000000, and the peripherals are at 0xFE000000 (the "RAM from 1 GByte" assumption
   in the kernel map has to go). `config.txt`: `arm_64bit=1`, `enable_uart=1` (or the PL011 on GPIO 14/15).
2. UART: the PL011 driver of the virt machine with the Pi's base address and clock (48 MHz) and GPIO pin setup.
3. Timer and interrupts: the generic timer and GICv2 as now (the Pi's GIC is at 0xFF840000 and in the device tree).
4. SD card: SDHCI (Arasan, "EMMC2") in polling mode instead of virtio-blk; the disks become partitions or files on the card.
5. More CPUs: spin-table or PSCI (depends on the firmware / armstub), start with one CPU.
6. Real time clock: there is none; start from a fixed date.
7. Network (separate project, first in qemu `virt` with virtio-net): Ethernet, ARP, IPv4, ICMP, UDP, a small TCP, a telnet
   server that attaches a session to a virtual terminal; then the GENET driver for the board.
**Decision (user, 2026-11-03): code from the BSD kernels and drivers (FreeBSD, NetBSD, OpenBSD) is adapted and used where it
saves work.** Every adapted file keeps its original copyright and license notice and gets a line saying where it came from
and what was changed; the notices go into the source file and into `arm64/README.md` (list of third-party code). Check the
license header of each file first (BSD-2/3-Clause is fine; do not take files that are GPL-2.0-only).
BSD files looked at on 2026-11-03 (all BSD-2/3-Clause, FreeBSD `main`; they are written for FreeBSD's newbus, `bus_space`,
mbuf and mmc stack, so what is taken are the register definitions, the initialisation sequences and the transfer logic, not the
glue around them):
`sys/dev/sdhci/sdhci.c` (84 KB) and `sdhci.h` (18 KB), `sys/arm/broadcom/bcm2835/bcm2835_sdhci.c` (24 KB, the Pi's SDHCI glue) and
`bcm2835_mbox.c` (15 KB, the firmware mailbox: clocks, power); `sys/arm64/broadcom/genet/if_genet.c` (46 KB) and `if_genetreg.h`
(10 KB, the 4B's Ethernet); `sys/dev/uart/uart_dev_pl011.c` (the PL011); `sys/arm/arm/gic.c` (GIC, BSD-3-Clause).
Sources to learn from: FreeBSD and NetBSD (BSD license: the drivers may be adapted), U-Boot (mostly GPL-2.0-or-later,
compatible with this project's GPLv3), the Broadcom datasheets. Linux is GPL-2.0-only and must not be copied into this
GPLv3 project (reading it for facts such as register layouts is fine).

### Raspberry Pi 4B: progress (branch `rpi4`; plan and principles: `PRINCIPLES.md`)

- **Step 1, first part, done on 2026-11-04:** the kernel boots in qemu's `raspi4b` machine (`make run-rpi4`): `start.S` takes the
  RAM block from the address it runs at (qemu virt: 0x40080000, Pi: 0x80000) instead of assuming RAM at 1 GByte, the other RAM and
  all devices are mapped from the device tree (`platform_map`), `make kernel8.img` links the image for load address 0x80000, a board
  without a PL031 clock starts at 1 January 2026 (`arch_clock.c`). In qemu: RAM 960 MByte at 0, GIC-400 at 0xff841000, timer, PL011
  at 0xfe201000, MMU self-test pass; the disk tests fail because there is no SD driver yet. `make pcheck` for the qemu virt machine
  is unchanged.
- `rpi4-qemu.dts` is a hand-written device tree for qemu's raspi4b (qemu has no firmware that provides one).
- **Still open in step 1 (needs the board or the real firmware tree):**
  - The real firmware device tree describes peripherals with *bus addresses* (0x7e201000) and a `ranges` property that translates them
    to 0xfe201000; `dt.c` ignores `ranges` and would read the wrong addresses. It needs address translation (a few lines per level).
  - Block 3 (0xC0000000-0xFFFFFFFF) holds the peripherals at 0xFE000000 but also RAM on the 4 GByte and 8 GByte models (up to
    0xFC000000); the boot table and `platform_map` map a whole GByte either as RAM or as device memory. The 1 GByte model that was
    ordered does not have this problem. For the bigger models the peripheral window has to be mapped with finer granularity.
  - Starting the other three CPUs (the firmware's spin table or PSCI), the interrupt numbers of the mini UART, boot on the board with
    `config.txt` (`arm_64bit=1`, `enable_uart=1`).

### Then

1. Replace this file when everything above is done: delete `TODO.md` and put the short list of remaining limitations
   (see "Differences that remain") into `README.md`. The notices that the port is not the work of the original authors
   are already in the READMEs, `ARM64-PORT.md` and the book appendix.
2. Before the repository is made public: check the notices again; `bin-build/bigdisk.img` (98 MByte) is in the history
   of the original repository.

## Missing functionality

- [x] **Pthreads**: `pthread_create`, `pthread_exit` and the `pthread_mutex_*` calls work like in the book (threads
      share the address space, the refcount keeps it alive until the last thread has gone; no `pthread_join`).
      Tests: `thtest64` (kernel tests) and the book's `thread 1` in the shell test. The user programs are now
      built with `-O0` like on the x86 (with `-O1`, `thread` never leaves its wait loop on plain globals).
- [x] **Buffer cache of the book**: `buffer_read`, `buffer_write`, `buffer_sync`, `buffer_contains` and `writeblock`
      are the book's code (512 blocks, dirty flag, least-often-used replacement). Like on the x86 the blocks go to
      the disk with `sync` (or when the cache is full); the book has no periodic flush either. The kernel's own
      Minix reader (`minixfs.c`) goes through the cache, too. Tests call `buffer_sync` before `make verify`.
- [x] **Swap**: `page_out`, `page_in`, the swap file `/tmp/swap`, the page replacement counters (timer tasks), `free_a_frame`,
      `get_free_frames` with the swapper lock and the real `/bin/swapper` (PID 2) work like in the book, written for the
      4-level page tables (`arch_swap.c`; the book's `write_swap_page`, `read_swap_page`, `paging[]`, `counter_table` are
      used unchanged). Differences that the hardware forces: the accessed bit is emulated with the AArch64 access flag
      (cleared by the counter update, set again by the fault handler), and the swap file is created when it does not
      exist and grows with the pages written (the 4 MByte disk has no room for the book's 64 MByte file). Pages that are
      shared after `fork` (copy-on-write) are never paged out. Test: `swaptest64` (pages out, kernel access to paged-out
      pages, fork, `free_a_frame`).
- [x] **VGA-only functions**: `read_screen`, `write_screen`, `get_xy`, `set_xy`, `clrscr` and the output of `kputch` work like
      in the book on a model of the screen (`vt[]`, 80 x 24 characters with color attribute, cursor), see `arch_vt.c`.
      The serial console is the "screen": set_xy, clrscr, write_screen and switching terminals repaint it with ANSI
      sequences (cursor positioning, colors, a scroll region of 24 lines). The book's `vi` works. The status line (line 25) is
      painted too (see "Done on 2026-11-03"); the console must understand ANSI sequences.
- [x] **Kernel shell commands for x86 hardware**: `mem` dumps the 4-level tables of the current address space, `test` shows
      the page table data (TTBR0/TTBR1, root table, frames), `rp` releases a page range of the current address space with
      the book's `release_page_range`. `div0` raises the exception by hand (`brk`), because an integer division by zero
      does not trap on AArch64; like on the x86 it ends in the kernel shell.
- [x] `recurse` (in `lib-build/tools`) runs: the user mode stack grows by one page when the page below it is touched
      (like the book's page fault handler; limit 64 MByte). The build replaces the x86 register name in its inline
      assembler (`mov %esp` -> `mov sp`), nothing else. Test: `stacktest64` and `recurse` in the shell test.

## Hardware and robustness

- [x] **Other boards and more than one CPU** (tested with qemu only):
      - The kernel reads the device tree (`dt.c`): RAM (64 MByte to 2 GByte tested; the kernel map grows with it), CPUs,
        PSCI, interrupt controller, UART, clock, timer and the virtio devices. Without one the qemu virt defaults are used.
        qemu hands the tree over only to images in the Linux arm64 boot format, so the kernel is also built as `*.bin`
        (header at the start, `text_offset` 0x80000); `make check` and the run targets use those.
      - GICv2 and GICv3 (`gic.c`), chosen by the device tree; EL1 and EL2 start (EL2: `ICC_SRE_EL2` and the timer are enabled).
      - More than one CPU (`smp.c`): secondary CPUs start with PSCI `CPU_ON`. The book's kernel has no locks, so the CPUs
        share it with one big kernel lock; user programs run in parallel. The book's `current_task` and `current_as` are per CPU
        (swapped in at the lock), the scheduler is wrapped (threads of other CPUs are out of the ready queue while it
        chooses), every CPU has its own idle context, signals for threads on other CPUs are handed over. Tested with 1, 2 and 4 CPUs.
      - Cache maintenance for DMA in `virtio.c` (always done; on coherent systems it only costs time). Not testable with qemu.
      - Not done: CPU hot plug, power management, CPUs that are not in the device tree, more than one GByte-aligned RAM region,
        a kernel with a different load address.
- [x] **User pointers in system calls**: like on the x86 (the book's page fault handler treats a fault inside
      kernel code as a kernel error) an exception in the kernel stops the scheduler and jumps to the kernel shell
      (`arch_kernel_fault_shell`). A bad pointer in a system call therefore does not end just the process on
      either platform. Test: `badptr64` in the shell test.
- [x] Kernel stacks of finished processes are freed at the next `fork` (`arch_threads_reap`), and per-process
      ASIDs replace the TLB flush at address space switches (needs 16-bit ASIDs, else the old flush is used).
- [x] Interrupt-driven disk transfers like the IDE driver of the book: a process takes `hd_lock`, starts the request and sleeps in
      `harddisk_queue`; the interrupt of the virtio device wakes it. One request at a time, as in the book. The idle thread,
      the boot code and the tests poll. The loader (`exec`) and the swap file are serialized with locks, because the
      ARM64 code around them uses static buffers.
- [x] Output of a terminal that is not visible is kept as its screen (24 lines, like on the x86), not in a 4 KByte buffer.

## Book and documentation

- [x] **Chapter about the ARM64 port in `ulix-book.nw`**: the appendix "The ARM64 Port (an Unofficial Extension)" (before "Introduction to C").
      It starts with the notice that it was not written by the authors of the book. `notangle` gives the same `ulix.c` and
      `start.asm` as before; `noweave` accepts it; the appendix alone was typeset with `tectonic` (installed with brew; the whole book
      needs fonts that are not on this machine, so `make pdf` was not run).
- [ ] Check the x86 build with the toolchain the Makefiles name (`gcc-4.4`, `i586-elf-gcc`). Not possible here: that compiler is not
      available on this machine; all checks so far used GCC 16 (`i686-elf-gcc`) and qemu, and `notangle` gives the same `ulix.c`
      and `start.asm` before and after the appendix was added.
- [ ] Further 64-bit clean-up in the book (not done on purpose): `ulixlib.c` still passes pointers as `(uint)` to the system call
      functions. That works because user addresses are below 2 GByte, and changing the library would touch the x86 build, too.
      The warnings are switched off in `userland/Makefile`.
- [ ] **Publish the branch `arm64-port` as a fork in the user's own GitHub repository** (no pull request to the original
      repository). `origin` points to the original author's repository (`hgesser/ulix`) and must not be pushed to.
      - [x] Notices that the port is *not* the work of the original authors, who have nothing to do with it: top of the main
        `README.md`, `arm64/README.md`, `ARM64-PORT.md` (German) and the first paragraph of the book appendix.
      - [x] The original license (GPL v3, `LICENSE`) and the copyright notices are untouched.
      - [x] Private repository `MatthewJer/ulix-arm64` created (2026-11-03) with the notice as its description; `arm64-port` pushed.
        Not a GitHub fork (a fork of a public repository cannot be private). Make it public only after checking the notices.
