# Ulix on ARM64

> **Notice: not the work of the original authors.** This port to AArch64 is part of an unofficial fork of Ulix. It was **not
> written by the authors of Ulix** (Hans-Georg Eßer and Felix Freiling), who have nothing to do with it, did not review or
> endorse it and are not responsible for it. The original project is at <https://github.com/hgesser/ulix>. Licence: GNU GPL
> version 3, like the original (see `../LICENSE`).


This directory contains a port of the Ulix kernel (the code in [`../ulix-book.nw`](../ulix-book.nw)) to 64-bit ARM
(AArch64). It runs on the `virt` machine of qemu (`qemu-system-aarch64`), also on Apple-silicon Macs, and boots
the same system as the x86 version: `/init` → `/bin/login` → `/bin/sh`, with the programs of `lib-build/tools`.

**The x86 version is unchanged and still builds** (`make` in the top-level directory). Everything that the port
needed in the book is written so that the x86 code stays as it was: either behind `#if defined(__i386__)`, or
with types and macros that mean the same on both processors. After every change to the book, the x86 object code
was compared function by function with the original, and the x86 kernel was booted in qemu. The only functions
that differed in the last comparison (`../ARM64-PORT.md`, section 18) were the ones in which a bug was fixed (see
[Bugs found in the book](#bugs-found-in-the-book)); that comparison has not been repeated for the later changes (swap, pthreads, the
serial disk, the network), which were written in the same way. (The network changes nothing in the book; it is only in `arm64/`.)

## Quick start (macOS)

```sh
brew install qemu noweb aarch64-elf-gcc aarch64-elf-binutils   # python3 is needed, too
cd arm64
make                 # kernel images, user programs, disk image (disk.img)
make run-shell       # boots to the shell, with a network card (try  ping 10.0.2.2  or  ifconfig ); Ctrl-A X leaves qemu
make check           # all test suites one after the other (slow)
make pcheck          # the same tests as parallel qemu instances (about 4 minutes)
```

In the shell: `ls`, `cat`, `cd`, `cp`, `rm`, `mkdir`, `rmdir`, `touch`, `wc`, `grep`, `stat`, `ps`, `df`, `free`, `ping`, `ifconfig`, `route`, `dhclient`, `shutdown`, `poweroff`, `reboot`, …
(it is Ulix, so `echo` copies its input until you press Ctrl-C). `>` and `<` redirect. Keys:

| Key | Effect |
|---|---|
| Ctrl-C | ends the foreground program (like on x86) |
| Ctrl-T, then 1…9 or 0 | switches to another terminal (x86: Alt-1…0); other terminals buffer their output |
| Ctrl-] | stops the scheduler and opens the kernel shell (x86: Shift-Esc); `help`, `ps`, `queues`, `exit` |

To leave qemu: `poweroff` (or `shutdown now`) in the shell; `reboot` starts the system again. (qemu's own Ctrl-A X works in most terminals.)

Changes to the disk are discarded when qemu ends (`-snapshot`).

## How it works

The port keeps the book as the single source:

* `ulix-arm64.nw` is a small noweb file that **reuses the chunks of the book**. `notangle` produces the
  declarations, global variables and function implementations of the kernel; `tools/port.py` then picks the
  functions and globals listed in `port.manifest` (208 functions and 46 global variables: the Minix file system, the VFS layer,
  threads, scheduler, mutexes, semaphores, system calls, signals, the kernel shell, …). They are compiled
  unchanged into `gen/ulix-port.c`.
* What the book does for the x86 hardware is written again for ARM64 in the `arch_*` and driver files and has the
  same names and data structures. See the table below.
* The user space (`userland/`): `ulixlib`, `init` and the 60 programs of `../lib-build/tools` are compiled for
  AArch64, unchanged.

| x86 (book) | ARM64 (this directory) |
|---|---|
| GDT, IDT, TSS, `int 0x80`, `iret` | `vectors.S` (exception vectors, register frame), `svc #0`, `eret` |
| 8259 PIC, 8253 PIT | `gic.c` (GICv2 and GICv3), `irq.c`: generic timer (100 Hz) per CPU |
| 2-level paging, kernel at `0xC0000000` | `mmu.c`, `start.S`: 4 levels, kernel in the upper half at `0xFFFF000000000000 + physical` |
| fork/exec with kernel stacks inside the address space | `arch_thread.c`: separate kernel stack per thread, copy-on-write `fork`, `exec` with an ELF64 loader (`elf.c`) |
| context switch by swapping ESP/EBP/CR3 | `cpu_switch` (`vectors.S`) and `arch_context_switch` |
| signal delivery on the user stack | `arch_signal_check`, `sigreturn`, trampoline page |
| IDE / floppy | `virtio.c`, `glue.c`: up to three virtio-blk disks (hda, hdb, a stand-in for fd1), interrupt driven like the IDE driver; `minixfs.c`: small read-only reader used by the ELF loader |
| serial disk (`/dev/sdisk`, major 42; second COM port) | `glue.c`: second PL011 with interrupt, same protocol; `tools/serial_disk.py` serves the disk on the host (`make sdisk-test`) |
| VGA text mode, PS/2 keyboard, 10 virtual terminals | PL011 UART (`arch_console.c`); the screens of the terminals are a model of the VGA text screen (`arch_vt.c`) shown on the console with ANSI sequences |
| device detection by BIOS/ports, one CPU | `dt.c`: device tree; `smp.c`: more than one CPU (PSCI, big kernel lock) |
| swap file, page replacement | `arch_swap.c`: for the 4-level tables, with the access flag as accessed bit |
| CMOS clock | PL031 RTC (`arch_clock.c`) |
| no network in the book | an extension: virtio-net (`virtionet.c`), Ethernet, ARP, IPv4, ICMP, UDP, TCP (`net.c`, `tcp.c`), DHCP, a network thread (`netd.c`), BSD-style `ifconfig`/`route`/`dhclient`/`ping` (`userland/`) |

The kernel runs with interrupts masked, like the x86 kernel: the queues and tables of the book need no extra
locking. A timer interrupt switches threads when it arrives in user mode or in the idle thread.

## Files

```
ulix-arm64.nw    chunks that reuse the book         glue.c          block I/O, console output, shell replacements
port.manifest    which book functions are used      arch_thread.c   threads, address spaces, fork, exec, syscalls, signals
tools/port.py    selects them                       arch_console.c  UART input, Ctrl-C, kernel shell
start.S          boot, MMU on                       arch_clock.c    clock
vectors.S        exceptions, context switch         irq.c           interrupts, GIC, timer
mmu.c            frames, page tables, COW           virtio.c        block device
elf.c            ELF64 loader                       minixfs.c       Minix reader (for exec)
userland/        user library, init, programs       user/           small test programs (hello, forktest, sigtest, …)
tools/           minix_add.py, minix_check.py (host-side file system tools), disk_expected.py, test drivers,
                 serial_disk.py (the host end of the serial disk), sdisk_test.py
*_test.c         kernel test suites
virtionet.c      network card (qemu virt)           netdev.c        which network card is there
net.c  tcp.c     Ethernet, ARP, IP, ICMP, UDP, TCP  net.h           what the network layers offer each other
dhcp.c netconf.c DHCP client, kernel command line   netd.c  echo.c  network thread, echo service
net_syscalls.c   system calls for ping, ifconfig, route, dhclient, poweroff, reboot
```

## Tests (`make check`, or `make pcheck` for the parallel version)

* **Kernel tests** (129 checks with one disk, 132 with two, 134 with three; run at EL1 with the legacy virtio transport and at EL2 with
  the modern one, on GICv2 and GICv3, with 1 to 4 CPUs and 64 MByte to 3 GByte of RAM):
  MMU, file system (read and write, up to the double indirect zone), threads (mutexes, semaphores), ELF loader,
  `fork`/`exec`/`waitpid`, copy-on-write (measured by counting free frames), signals, faults in user programs.
* **`make verify`**: the write tests run on a copy of the disk; afterwards `tools/minix_check.py`, an independent
  implementation, checks the file system (tree, bitmaps, link counts, leaked blocks) and the contents of the files.
* **`make console-test`** (34 checks) types into the UART like a person; **`make shell-test`** (93 checks) runs a whole
  session: boot, auto login, the programs of `lib-build/tools`, Ctrl-C, terminal switching, `exit`, login with password.
* **`poweroff_test.py`** (job `poweroff`): `shutdown` and `poweroff` end qemu by themselves, `reboot` starts the kernel again.
* The shell test has a variant with a network card (`make pcheck`, job `shellnet`): the program `ping` in the Ulix shell.
* The network is configured like on a BSD: the kernel command line (`ip=dhcp`, `ip=10.0.0.5/24 gw=… dns=…`), and as root (`su`) the
  programs `ifconfig`, `route` and `dhclient`; the tests are the `shellnet` and `netconf` jobs of `make pcheck`.
* **`make netd-test`**: the network thread. The console kernel with the command line `netecho` has nothing but the thread to answer; Python
  sockets of the host get UDP and TCP echo back (up to 20000 bytes) while the kernel runs its program.
* **`make net-test`**: the network. The kernel (own image `ulix-net.bin`) runs on qemu's user-mode network and a Python program on the
  host talks to it with ordinary sockets: ping of the router, UDP echo, TCP echo of 100 and 20000 bytes, and the kernel as a TCP client
  that loses its first SYN and its first data segment on purpose and must send them again.
* **`make sdisk-test`**: the kernel tests with a second serial port that `tools/serial_disk.py` serves as a disk (the book's serial
  disk, `/dev/sdisk`); block I/O, the Minix code of the book on it, the buffer cache, two threads reading at the same time, and a
  check of the image on the host afterwards.
* The expected values for sizes and checksums are computed on the host by the independent parser, so the kernel is
  not compared with itself.

## Bugs found in the book

Running the code on a second processor and under a modern compiler showed problems that were already in the x86
version. They are fixed in `../ulix-book.nw` (x86 and ARM64 profit). The last column links to the place in the *original*
`ulix-book.nw` (upstream commit `32535a1`, the state this fork started from); line numbers refer to that file, not to the
current one.

| # | Bug | Where it was in the original |
|---|---|---|
| 1 | `wait_semaphore`/`signal_semaphore` worked on a *copy* of the semaphore, so they never worked. | `kl_semaphore sem = …` in [wait_semaphore](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18163-L18164) and [signal_semaphore](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18186-L18187); the same pattern is in the inactive variant at [19413–19426](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L19413-L19426) |
| 2 | `u_open` and `u_chdir` used a `struct stat` that stays uninitialised when the file does not exist: depending on the stack contents `open (..., O_CREAT)` could open the root directory. | `u_open`: [20519–20521](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20519-L20521) (chunk "handle symlink"); `u_chdir`: [21444–21445](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L21444-L21445) |
| 3 | `mx_open` took a *closed* entry of its inode table for an open file; a new file with the inode number of a deleted one inherited the old size and blocks. | search loop in `mx_open`, [22860–22861](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L22860-L22861) (compares only `ino` and `device`, not `refcount`; `mx_close` starts at [22975](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L22975)) |
| 4 | `mx_rmdir` removed `.` and `..` of the *parent* directory instead of the removed one. | [24016–24022](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L24016-L24022) |
| 5 | `syscall_waitpid` wrote through a null status pointer (`waitpid (pid, 0, 0)`, as `login` does). | [11713](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11713) (`status = r->ecx`) and [11729](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11729) (`*status = …`) |
| 6 | `printf` of the user library read its arguments from the stack (x86 calling convention only). | [29393–29399](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L29393-L29399) (`varg = &format`) and `print` at [29356](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L29356) |
| 7 | Several 32-bit assumptions: `memaddress`, `size_t`, `hexdump`, `eax_return` (pointers cut to 32 bits). | [size_t](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L2217) and [memaddress](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L2228) (typedefs), [eax_return](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L9215) (cast to `unsigned int`), [hexdump](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L29851) (`uint` parameters) |

### Bugs of the book that are *not* fixed

Found while porting, present on the x86 as well, left as they are in the book:

| Bug | Where |
|---|---|
| `mx_ftruncate` frees the data zones in the bitmap but keeps the zone pointers and the indirect blocks in the inode (the two chunks for the indirect blocks are never written; the book says so itself). When the file grows again, old and new pointers meet. | [mx_ftruncate](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23825-L23866), the missing chunks are used at [23853](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23853) and [23856](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23856), the book's remark is at [23868](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23868) |
| `mx_unlink` of a file with double indirect zones (more than about 263 KByte) leaves zones marked wrongly (found with `tools/minix_check.py`; the cause has not been analysed). | chunk "free this inode: (3) double indirect blocks", [23705–23729](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23705-L23729) |
| `u_write` returns -1 after writing to STDOUT/STDERR: the `case` has no `return`/`break` and falls into `default`. The output itself works. | [write: standard I/O](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20713-L20731) |
| `mx_mkdir` does not check whether the directory exists (`u_mkdir` does); a second `mx_mkdir` on an existing path would damage the file system. | [23942](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23942) |
| `u_kill` on a blocked parent process leaves children that end later as zombies; `SIGKILL` sets `used = false` at once, so a following `waitpid` reports -1. | [27781](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L27781) |

## Not done

* Everything has only been run on qemu (`-M virt`, GICv2 and GICv3, 1 to 4 CPUs, EL1 and EL2). Real boards are untested: the device
  tree reader, the cache maintenance for DMA and the PSCI start of the other CPUs are written for them but could not be tried.
  VirtualBox cannot run this on an ARM host; use qemu.
* The network is small on purpose: no UDP or TCP sockets for user programs (only raw ICMP, which is what `ping` needs), the DHCP lease is not
  renewed, no name resolution, one interface, no fragmentation, no congestion control in TCP.
* `/dev/fd0` has no drive behind it (a stand-in for the second floppy, `/dev/fd1`, is the only one).
* Not checked: the x86 build with the compiler that the makefiles name (`gcc-4.4`); all checks used GCC 16 (`i686-elf-gcc`). `ulixlib.c` still
  passes pointers as `uint` to the system call functions; that works because user addresses are below 2 GByte, and changing it would touch the
  x86 build.
* No `pthread_join` (the book has none either); `exit ()` and `exec ()` in threads behave like in the book.
* The book's kernel is single-processor code. More than one CPU is done with one big kernel lock (see `smp.c`): user programs run in
  parallel, the kernel does not.
* The book has one appendix about the port ("The ARM64 Port (an Unofficial Extension)"); `ulix-arm64.nw` and `../ARM64-PORT.md` (a log of
  the work with the reasoning behind each step, translated from German) go into more detail. The whole book could not be typeset on the machine
  used for the port (`make pdf` was not run).
