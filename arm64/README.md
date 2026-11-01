# Ulix on ARM64

This directory contains a port of the Ulix kernel (the code in [`../ulix-book.nw`](../ulix-book.nw)) to 64-bit ARM
(AArch64). It runs on the `virt` machine of qemu (`qemu-system-aarch64`), also on Apple-silicon Macs, and boots
the same system as the x86 version: `/init` → `/bin/login` → `/bin/sh`, with the programs of `lib-build/tools`.

**The x86 version is unchanged and still builds** (`make` in the top-level directory). Everything that the port
needed in the book is written so that the x86 code stays as it was: either behind `#if defined(__i386__)`, or
with types and macros that mean the same on both processors. After every change to the book, the x86 object code
was compared function by function with the original, and the x86 kernel was booted in qemu. The only functions
that differ are the ones in which a bug was fixed (see [Bugs found in the book](#bugs-found-in-the-book)).

## Quick start (macOS)

```sh
brew install qemu noweb aarch64-elf-gcc aarch64-elf-binutils   # python3 is needed, too
cd arm64
make                 # kernel images, user programs, disk image (disk.img)
make run-shell       # boots to the shell; Ctrl-A X leaves qemu
make check           # all test suites (about 2 minutes)
```

In the shell: `ls`, `cat`, `cd`, `cp`, `rm`, `mkdir`, `rmdir`, `touch`, `wc`, `grep`, `stat`, `ps`, `df`, `free`, …
(it is Ulix, so `echo` copies its input until you press Ctrl-C). `>` and `<` redirect. Keys:

| Key | Effect |
|---|---|
| Ctrl-C | ends the foreground program (like on x86) |
| Ctrl-T, then 1…9 or 0 | switches to another terminal (x86: Alt-1…0); other terminals buffer their output |
| Ctrl-] | stops the scheduler and opens the kernel shell (x86: Shift-Esc); `help`, `ps`, `queues`, `exit` |

Changes to the disk are discarded when qemu ends (`-snapshot`).

## How it works

The port keeps the book as the single source:

* `ulix-arm64.nw` is a small noweb file that **reuses the chunks of the book**. `notangle` produces the
  declarations, global variables and function implementations of the kernel; `tools/port.py` then picks the
  functions and globals listed in `port.manifest` (193 functions and 39 global variables: the Minix file system, the VFS layer,
  threads, scheduler, mutexes, semaphores, system calls, signals, the kernel shell, …). They are compiled
  unchanged into `gen/ulix-port.c`.
* What the book does for the x86 hardware is written again for ARM64 in the `arch_*` and driver files and has the
  same names and data structures. See the table below.
* The user space (`userland/`): `ulixlib`, `init` and the 59 programs of `../lib-build/tools` are compiled for
  AArch64, unchanged.

| x86 (book) | ARM64 (this directory) |
|---|---|
| GDT, IDT, TSS, `int 0x80`, `iret` | `vectors.S` (exception vectors, register frame), `svc #0`, `eret` |
| 8259 PIC, 8253 PIT | `irq.c`: GICv2, generic timer (100 Hz) |
| 2-level paging, kernel at `0xC0000000` | `mmu.c`, `start.S`: 4 levels, kernel in the upper half at `0xFFFF000000000000 + physical` |
| fork/exec with kernel stacks inside the address space | `arch_thread.c`: separate kernel stack per thread, copy-on-write `fork`, `exec` with an ELF64 loader (`elf.c`) |
| context switch by swapping ESP/EBP/CR3 | `cpu_switch` (`vectors.S`) and `arch_context_switch` |
| signal delivery on the user stack | `arch_signal_check`, `sigreturn`, trampoline page |
| IDE / floppy | `virtio.c`: virtio-blk; `minixfs.c`: small read-only reader used by the ELF loader |
| VGA text mode, PS/2 keyboard, 10 virtual terminals | PL011 UART (`arch_console.c`); terminals are kept apart in software |
| CMOS clock | PL031 RTC (`arch_clock.c`) |

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
tools/           minix_add.py, minix_check.py (host-side file system tools), disk_expected.py, test drivers
*_test.c         kernel test suites
```

## Tests (`make check`)

* **Kernel tests** (104 checks, run at EL1 with the legacy virtio transport and at EL2 with the modern one):
  MMU, file system (read and write, up to the double indirect zone), threads (mutexes, semaphores), ELF loader,
  `fork`/`exec`/`waitpid`, copy-on-write (measured by counting free frames), signals, faults in user programs.
* **`make verify`**: the write tests run on a copy of the disk; afterwards `tools/minix_check.py`, an independent
  implementation, checks the file system (tree, bitmaps, link counts, leaked blocks) and the contents of the files.
* **`make console-test`** (21 checks) types into the UART like a person; **`make shell-test`** (63 checks) runs a whole
  session: boot, auto login, about 25 commands, Ctrl-C, terminal switching, `exit`, login with password.
* The expected values for sizes and checksums are computed on the host by the independent parser, so the kernel is
  not compared with itself.

## Bugs found in the book

Running the code on a second processor and under a modern compiler showed problems that were already in the x86
version. They are fixed in `../ulix-book.nw` (x86 and ARM64 profit):

1. `wait_semaphore`/`signal_semaphore` worked on a *copy* of the semaphore, so they never worked.
2. `u_open` and `u_chdir` used a `struct stat` that stays uninitialised when the file does not exist: depending on the
   stack contents `open (..., O_CREAT)` could open the root directory.
3. `mx_open` took a *closed* entry of its inode table for an open file; a new file with the inode number of a deleted one
   inherited the old size and blocks.
4. `mx_rmdir` removed `.` and `..` of the *parent* directory instead of the removed one.
5. `syscall_waitpid` wrote through a null status pointer (`waitpid (pid, 0, 0)`, as `login` does).
6. `printf` of the user library read its arguments from the stack (x86 calling convention only).
7. Several 32-bit assumptions: `memaddress`, `size_t`, `hexdump`, `eax_return` (pointers cut to 32 bits).

## Not done

* No pthreads (`thread` and `mutex` in `lib-build/tools` do not run), no swap, no VGA functions (`vi` does not work as
  an editor, `clrscr` uses ANSI codes), `recurse` (x86 assembler).
* The buffer cache of the book is not used: `writeblock` writes through to the disk. (It would delay writes, and the
  tests that check the disk image from outside need them to be there.)
* Only the `virt` machine of qemu. GICv3, device tree parsing (the 128 MByte of RAM are fixed) and cache maintenance
  for DMA on real hardware are missing. VirtualBox cannot run this on an ARM host; use qemu.
* The port is not yet described in the book itself; `ulix-arm64.nw` and `../ARM64-PORT.md` (in German, a log of the work with the
  reasoning behind each step) are the documentation for now.
