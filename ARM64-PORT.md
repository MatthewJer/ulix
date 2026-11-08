# Ulix → ARM64: inventory and work log

> **Notice: not the work of the original authors.** This document belongs to an unofficial fork of Ulix that contains a port to
> AArch64. The port and this document were **not written by the authors of Ulix** (Hans-Georg Eßer and Felix Freiling). They have
> nothing to do with the fork, have neither reviewed nor endorsed it and are not responsible for it. The original project:
> <https://github.com/hgesser/ulix>. Licence: GNU GPL version 3 as in the original (see `LICENSE`).

State: branch `arm64-port`. This document is a **work log**: sections 1 to 4 are the analysis that was needed before the port; from section 5 on follows the log of the stages. Numbers, test counts and statements like "still missing" hold for the state of the stage in which they appear. The port now runs completely in qemu; the current state is described by `arm64/README.md` and the last section of this file (section 21: open points; there is no `TODO.md` any more). The network stack (section 20) comes from the branch `rpi4`, which prepares the Raspberry Pi 4B. The log was originally written in German and has been translated into English.
All line numbers refer to the **original** `ulix-book.nw` (32 557 lines, upstream commit `32535a1`; the book in this fork has grown since). The book is the only source: `ulix.c`, `start.asm` and `ulix.ld` are generated from it with `notangle`.

## 1. Why this is not "translating instructions"

Ulix is not a program that merely contains x86 instructions; it is a kernel for the **PC platform**. An ARM64 port therefore means rewriting the hardware layer:

| Area | x86 (Ulix today) | ARM64 equivalent | Effort |
|---|---|---|---|
| Boot | GRUB Legacy from floppy, Multiboot, `start.asm` (NASM) | UEFI/device-tree boot (e.g. qemu `-kernel`), new start code in GNU as syntax | high |
| Segmentation (GDT, 127 hits) | flat segments, ring 0/3 through selectors (0x08/0x10/0x18/0x20) | gone; privileges through EL1/EL0 | medium |
| Interrupts (IDT, 226 hits; PIC, 424 hits) | IDT + 8259 PIC, `iret` | exception vector table (`VBAR_EL1`) + GICv2/v3, `eret` | high |
| System calls | `int 0x80` (16 hits) | `svc #0` | low to medium |
| TSS (65 hits) | kernel stack switch on ring change | `SP_EL1`/`SP_EL0`, no TSS needed | medium |
| Paging (~281 hits) | 2 levels, 4 KB pages, 32 bit, `cr0`/`cr3` (33 hits) | 4 levels (48 bit), `TTBR0/1_EL1`, `TCR_EL1`, `MAIR_EL1` | **very high** |
| Process switch | registers + stack frame of `iret` | new context (x0–x30, SP, PC, PSTATE) | high |
| Port I/O (`inportb`/`outportb`, 90 hits) | `in`/`out` | does not exist; devices are memory-mapped (MMIO) | see 2 |
| Inline asm | 61 `asm` blocks | all to be rewritten | medium |
| Compiler | `i586-elf-gcc`, `-m32 -march=i586` | `aarch64-elf-gcc`; Ulix uses the `__sync_*` builtins, which work there | low |
| Word size | `int`/pointer = 32 bit, hard casts like `(unsigned)fdc_buf` | 64-bit pointers; every pointer-to-int cast and all structure sizes must be checked | high |
| User space | **ready-made x86 machine code bytes** in the book (line ~12758, hex dump of `test`), the ELF loader expects i386 ELF | must be recompiled, the loader changed to ELF64/AArch64 | medium |

### Devices that have no direct counterpart on ARM

The book is built around PC hardware, as a teaching device. On the ARM64 `virt` board (qemu):

| Ulix driver | Port/address today | Replacement |
|---|---|---|
| VGA text mode (line 16472, `0xB8000`) | text memory + CRTC ports 0x3D4/0x3D5 | PL011 UART as console, or a framebuffer (ramfb/virtio-gpu) |
| Keyboard (8042, ports 0x60/0x64, line 8146) | PS/2 | PL011 UART input, or virtio-input |
| Serial port (16550, COM1 0x3F8) | port I/O | PL011 (`0x09000000` on `virt`) |
| PIT 8253 (ports 0x40/0x43, line 16991) | 100 Hz timer | generic timer (`CNTP_*` registers), PPI interrupt |
| CMOS RTC (0x70/0x71, line 17015) | BCD time of day | PL031 RTC (MMIO) |
| PIC (0x20/0x21/0xA0/0xA1, line 7130) | 8259 | GIC |
| IDE/ATA PIO (ports 0x1F0–0x1F7, 0x3F6, line 25941) | hard disks | virtio-blk (MMIO); the ATA protocol itself goes away entirely |
| Floppy controller + ISA DMA (line 26504, 26833) | NEC 765 | gone; virtio-blk as a replacement |
| Memory size | hard-coded 64 MB (`MEM_SIZE`, line 5998) | read the RAM layout from the device tree |

In practice **every driver** (console, input, timer, clock, disk access, interrupts) has to be written again. The book would need new chapters for the port, not just changed lines. The core (scheduler, process and file system logic, Minix FS, system call tables) remains largely reusable.

## 2. What matters about VirtualBox and ARM

- On ARM hosts (Apple silicon) VirtualBox, to my knowledge, **does not emulate x86**; it only starts ARM guests there (please check against the current VirtualBox documentation). Whoever wants the port in order to run Ulix on an M-series Mac starts it with **qemu `-M virt -cpu cortex-a72`** (or UTM). VirtualBox is the wrong tool for that.
- The Bochs and qemu lines in the Makefile have to be replaced by `qemu-system-aarch64`.

## 3. What obviously causes problems under VirtualBox (x86)

The book itself says (lines 30742–30744) that Ulix only boots in qemu. Bochs and VirtualBox have never been tested. Conspicuous places, sorted by likelihood.

**Note:** I have read the places in the code (line numbers, ports, loops). How VirtualBox behaves there, however, is a **guess from experience with the hardware, not tested**. Please verify every point in a real VM.

1. **Floppy driver with ISA DMA** (lines 26504–26870). The driver writes directly to the 8237 DMA controller (ports 0x0A, 0x0C, 0x0B, 0x04/0x05, 0x81) and uses `outb_delay`. VirtualBox emulates the FDC, but timing and DMA behaviour differ from qemu. The reset/recalibrate and the `for (i < 10000/30000)` wait loops (lines 26526, 26562) depend on timing and may run out before the controller answers in VirtualBox.
2. **ATA detection in `ata_init`** (line 26420). The loop only checks `status != 0`. On a bus without a device, VirtualBox returns `0xFF` (floating bus) instead of `0`, depending on the chipset; then a drive is wrongly "detected" and `IDENTIFY` runs on nothing. In addition, the disks are assumed to be **primary master/slave** (0x1F0). When creating the VM, both disks must be attached to the IDE controller exactly like that.
3. **Hard-coded 64 MB of RAM** (`MEM_SIZE`, line 5998), plus a fixed memory map (line 910). The VM must not have more than 64 MB, or the rest stays unused; with less the kernel crashes. The Multiboot memory information is not evaluated.
4. **CMOS clock** (lines 17023–17036). It reads register 0x32 (century) without checking. That register is not necessarily populated in VirtualBox (it may return 0, then the year is wrong), and the update-in-progress flag (register 0x0A) is never checked, so intermediate values are possible. Also: the code assumes BCD and does not read register 0x0B. VirtualBox also has `--rtcuseutc`, so the time can be shifted by the time zone.
5. **Serial port** (lines 16901, 17303). The loops wait at most 128 times for the transmit bit (`LSR & 0x20`); if COM1 is not enabled in VirtualBox, debug output is silently discarded. Then you see nothing on a crash. The VM setting `--uart1 0x3F8 4` is therefore mandatory.
6. **Boot with GRUB Legacy from floppy** (`MENU.LST`, `ulixboot.img`). In VirtualBox this only works if the floppy image is the primary boot device, and it is an unchecked path.
7. **PIT constant and interrupt rate** (lines 16978–16998). `1193180 / hz` is right, but the code assumes an exactly periodic IRQ0. VirtualBox loses ticks under load; that is a timing problem, not a crash problem.
8. **CPU assumptions**: `-march=i586` (compiler flags) and the `__sync_*` atomics (line 19439) produce `lock cmpxchg`. That runs in VirtualBox. If VT-x/AMD-V is off there, a guest with paging in the real-mode transition usually still starts, only slower.
9. **Untested compiler chain**: the Makefile has fixed paths `/usr/bin/gcc-4.4` (Linux) and `/usr/cross/bin/i586-elf-*` (macOS). Neither exists on modern systems; GCC 4.4 is 14 years old, and with newer versions inline asm and `-fno-function-cse` probably break.

## 4. Proposed procedure

A complete port is a project of several weeks. Sensible stages, each runnable on its own:

1. **Toolchain**: make the Ulix build reproducible on the Mac (`aarch64-elf-gcc`, `noweb`, a script instead of fixed paths).
2. **Bare-metal skeleton** for `qemu-system-aarch64 -M virt`: start code, stack, PL011 output, get Ulix's `printf` running.
3. **Exceptions + GIC + generic timer**: replaces IDT/PIC/PIT.
4. **MMU/paging** with 4 levels: the most laborious part, because Ulix's address space logic is firmly designed for 32 bit / 2 levels.
5. **Processes and system calls** (`svc`, EL0/EL1): adapt the scheduler and the context switch.
6. **Mass storage**: virtio-blk instead of IDE/floppy, reuse the Minix file system.
7. **Recompile user space** (AArch64 ELF) and adapt the ELF loader.

In parallel, work through the list from section 3 if Ulix is also supposed to keep running on x86/VirtualBox.

## 5. Status of stage 1 (reproducible build)

Done and tested on macOS/arm64 (Homebrew: `noweb nasm mtools i686-elf-gcc qemu`):

- `bin-build/assembler-parser.py`: ported from Python 2 (`file()`) to Python 3.
- `bin-build/Makefile`: cross compiler overridable with `CROSS=`, default `i686-elf-` from the PATH instead of `/usr/cross/bin/i586-elf-`; module extraction only if `bachelorarbeit.nw` exists (the file is missing from the repository; before, `module.c` was truncated to 0 bytes).
- Result: `make build` produces a `ulix.bin` (ELF32 i386, 200 KB) with GCC 16.2. The kernel boots in `qemu-system-i386` (64 MB, floppy + 2 disks) up to "swapper launched in background", serial output checked.
- Not checked: keyboard/VGA interaction, user programs in the shells, `make pdf`.
- `notangle` still reports "undefined chunk name" warnings and exit code 2 (book errors, already ignored in the original with `; true`).
- The repository contains generated files (`ulix.c`, `*.o`, `ulix.bin`, `*.img`); a build in the directory changes them. I built in a copy.

## 6. Status of stage 2 (bare-metal skeleton)

New directory `arm64/` (deliberately still outside the literate-programming source `ulix-book.nw`; the move into the book comes once the structure is settled):

- `start.S`: boot on core 0 (the other cores park), drop from EL2 to EL1, FP/SIMD on, stack, clear `.bss`, jump to `kmain`.
- `ulix.ld`: load address `0x40080000` (RAM start of qemu `virt` + kernel offset).
- `kmain.c`: PL011 UART (`0x09000000`) as console, small `printf` (`%s %c %d %u %x`, `l`, width/0 padding).
- `Makefile`: `make` and `make run` (`qemu-system-aarch64 -M virt -cpu cortex-a72`).

Tested: start both at EL1 (default) and at EL2 (`virtualization=on`); the output shows EL1, `sizeof(void*) = 8`. Not yet present: exceptions, timer, MMU (stages 3–4). The linker warns about an RWX segment, which is harmless until the MMU stage.

## 7. Status of stage 3 (exceptions, GIC, timer)

In `arm64/`:

- `vectors.S`: exception vector table (16 entries, `VBAR_EL1`), saves x0–x30, `ELR`, `SPSR` in a 272-byte frame and calls `exc_handler (frame, kind)`; return with `eret`. Replaces the x86 IDT together with `iret` frames.
- `irq.c`: GICv2 (distributor `0x08000000`, CPU interface `0x08010000` on qemu `virt`) replaces the 8259 PIC; the generic timer (`CNTP_TVAL/CTL`, PPI 30, 62.5 MHz in qemu) replaces the PIT and ticks at 100 Hz like `HZ` in the x86 kernel. `svc #0` (EC 0x15) is recognised as the system call entry and returns; all other synchronous exceptions print ESR/FAR/ELR and halt.

Tested in qemu (`virt`, Cortex-A72): 3 timer seconds of 100 ticks each (`wfi` loop), `svc #0` returns, `brk #0` lands in the handler with EC=0x3c.

Not tested/not present: FP/SIMD registers are not saved in the frame (the kernel builds with `-mgeneral-regs-only`); exceptions from EL0 and a separate stack per context come with the processes (stage 5); GICv3 (needed for `-M virt,gic-version=3` and real hardware) is missing; SMP is not used.

## 8. Status of stage 4 (MMU and paging)

The kernel now runs in the upper half of the address space (`TTBR1_EL1`), virtual address = `0xFFFF000000000000` + physical address. That corresponds to the role of `0xC0000000` (kernel) and `0xD0000000` (linear RAM window) on x86.

- `ulix.ld`: VMA `0xFFFF000040080000`, LMA `0x40080000`; entry through `_start_phys` (qemu jumps to the physical address with the MMU off).
- `start.S`: builds boot tables with PC-relative code (`adr`) (4 KB granule, 48 bit, 1 GB blocks: devices `0–1 GB` as Device-nGnRnE, RAM `1–2 GB` as Normal-WB) for `TTBR1` and an identity map for `TTBR0`, sets `MAIR/TCR/TTBR` and `SCTLR` (M, C, I), jumps to the high address, sets the stack, clears `.bss`. `IPS` is taken from `ID_AA64MMFR0_EL1`.
- `mmu.c`: frame allocator (bump pointer + free list, frames come zeroed), `map_page`/`unmap_page` over 4 table levels (creates missing tables, rejects double mappings and block collisions, TLB invalidation on unmap), `virt_to_phys` through the `AT` instruction (counterpart of `mmu_p`), `drop_identity_map` (`EPD0`).
- Drivers (`kmain.c`, `irq.c`) now reach the PL011 and the GIC through the linear mapping (`KERNEL_VA + 0x09000000` etc.).

Test in qemu (Cortex-A72, 128 MB, EL1 and EL2 start): map/unmap, AT translation, a write through the mapping visible in the linear window, double map rejected, reading an unmapped page → translation fault level 3 (FAR correct), writing to an RO page → permission fault level 3, frame reuse. All 10 checks pass.

Open and deliberately not done:
- The RAM size is still fixed (128 MB); it is supposed to come from the device tree later.
- The whole kernel sits in one RWX 1 GB block; the split into `.text` (RX), `.rodata` (RO), `.data` (RW, NX) on 4 KB pages is missing.
- `TTBR0` is empty; user-side address spaces (`address_spaces[]`, ASIDs; copying the kernel part is not needed, since `TTBR1` is global) come in stage 5.
- No copy-on-write, no swap (x86 Ulix has both): these parts of the book have to be rewritten for the new page table API.

## 9. Status of stage 5 (processes, system calls, EL0)

New in `arm64/` (shared definitions now in `ulix.h`):

- `mmu.c`: address spaces per process (`TTBR0`): `new_user_l0`, `map_page_in` (any root table), `free_user_space` (frees tables and data frames recursively), `sync_icache` (clean the D-cache, invalidate the I-cache after copying code), `user_mm_init` (empty `TTBR0`, `EPD0` off again). Page flags `PAGE_URX` (code, readable/executable from EL0) and `PAGE_URW` (stack, never executable). Counter `frames_in_use` for the leak test.
- `sched.c`: tasks with their own kernel stack (4 pages) and their own address space; round-robin scheduler; `cpu_switch` (in `vectors.S`) saves x19–x30, SP and `SP_EL0`. New processes start through a prepared frame on their kernel stack and `eret` to EL0. ASID = PID, hence no TLB flush on a switch. The idle task (slot 0) is the boot thread and cleans up zombies, since a process cannot free its own kernel stack.
- `irq.c`: system calls with `svc #0` (`x8` = number, `x0–x2` arguments): `write`, `getpid`, `yield`, `exit`. User pointers are never dereferenced directly: they are translated page by page with `AT` and read through the linear window. Errors in EL0 code (data/instruction abort) kill only the process. The timer interrupt ends the time slice after 10 ms (`schedule_preempt`), after it has been acknowledged with `EOIR`.
- `user.S`: two EL0 programs (position independent): "Hello" (5 rounds of `write` with a busy loop) and "bad" (reads kernel memory).

Test in qemu (Cortex-A72, EL1 and EL2 start): two Hello processes run interleaved (switches by timer preemption, 12–14 of 16–18 context switches), the bad process is ended with EC=0x24 / permission fault level 1 and the kernel keeps running, all three are cleaned up, `frames_in_use` is back at its initial value afterwards (no leak). Together with the tests of stage 4: all checks pass.

Open: the kernel is not preemptive (interrupts stay masked in the handler; that is enough as long as system calls are short); no `fork`/`exec`/`wait`, no signals, no ELF loader; the user programs are hand-written assembly in the kernel image (stage 7: load AArch64 ELF). The code is deliberately a new, small implementation of the Ulix concepts (task, address space, system call) and not yet the ported Ulix process management from `ulix-book.nw` (thread table, `fork`, signals, semaphores); setting that up is the main part of stage 5b.

## 10. Status of stage 6 (mass storage: virtio-blk + Minix)

New in `arm64/`:

- `virtio.c`: virtio-mmio block driver (polling) for the qemu `virt` machine; it looks for the device in the 32 MMIO slots from `0x0a000000` on and supports the legacy transport (version 1, qemu default) and the modern transport (version 2). One virtqueue with 8 entries; every request is a chain of header, data and status byte. Interface: `blk_read/blk_write (sector, buf, n)`, `blk_capacity`. This replaces IDE PIO and the floppy DMA driver of the x86 kernel (same job: read/write sectors).
- `minixfs.c`: Minix v2 file system (read only) on the block device, 14- and 30-character names, direct, single and double indirect zones, path resolution (`minix_lookup`), `minix_read`, `minix_readdir`. This is the same on-disk format as on x86 Ulix; `bin-build/minixdata.img` is used unchanged.
- `Makefile`: `make run` starts qemu with `-snapshot` and the image as a virtio disk (`DISK=` can be overridden). With `-snapshot`, writes only land in the temporary buffer; the image file in the repository stays unchanged (checked: `git status` stays clean).

Tested in qemu (Cortex-A72, EL1 and EL2 start, transport v1 and v2): device found, capacity 2880 sectors, superblock magic 0x2478, reading past the end of the disk is rejected, write and read back identical, mount, directory counts (`/` 16 entries, `/bin` 63), unknown path, file sizes and checksums of `/hello.c`, `/test.txt`, `/init` (uses the indirect zone) and `/bin/vi` (25 KByte, subdirectory). The reference values come from an independent Python parser on the host. 30 checks in total, all pass, including the tests of stages 4 and 5.

Deliberately open: the driver polls (no interrupt, no sleeping); DMA without cache maintenance, which is only correct on coherent platforms like qemu; only one request at a time; the file system is read only and has no buffer cache; there are no `open/read/close` system calls and no VFS yet (the mount table, `/dev`, `/tmp` and swap of the book are missing).

## 11. Status of stage 7 (ELF loader and AArch64 user space)

New in `arm64/`:

- `user/`: freestanding user space for AArch64 (`crt0.S`, `ulib.c/.h` with system call wrappers and `printf`, linker script `user.ld` with one `PT_LOAD` per permission class on its own 4 KB page) and three programs: `hello` (checks itself: `.data`, `.bss` over three pages, `.rodata`, recursion on the stack, `argc/argv`; exit status = number of failed checks), `cat` (`open/read/close`) and `crash` (writes to its own code page). Built without FP/SIMD, since the kernel does not save these registers on a context switch.
- `elf.c`: ELF64 loader (`ET_EXEC`, `EM_AARCH64`). Checks the header, segment limits and file size, maps the segments page by page (R+X → code, R → read-only, R+W → data/BSS, BSS comes from zeroed frames), rejects W+X segments (W^X), checks that the entry point lies in an executable segment, and cleans up on errors.
- `sched.c`: `task_create_elf (path, argc, argv)` creates the address space, the stack (4 pages) and `argc/argv` on the stack (`x0 = argc`, `x1 = argv`), a file descriptor table per process (8 entries), exit codes per PID.
- `irq.c`: new system calls `open`, `read`, `close`; `write` also for fd 2. User pointers go through `user_to_phys` (`AT S1E1R/W`): user range only, and when writing the page must really be writable.
- `tools/minix_add.py`: host tool that writes files into the root directory of a Minix v2 image (macOS has no `mkfs.minix`/mount). `make` uses it to produce `disk.img` from the x86 image plus programs; the original under `bin-build/` stays untouched. Two deliberately broken ELF variants (wrong architecture, RWX segment) are created for the loader tests.

Test in qemu (EL1 and EL2): 45 checks, all pass. Newly checked: programs start from the disk and run with the correct `argv`; the 7 self-tests in `hello`; `cat` reads 316 bytes and 1680 bytes (two zones) correctly, a missing file gives error code 1; `crash` is killed with a permission fault and the kernel keeps running; an ELF with the wrong architecture, a W+X segment, a flat x86 binary, a text file and a missing file are rejected (each with the right reason); afterwards no frame is lost.

Deliberately open: no `fork`/`exec`/`wait`/`spawn` from user mode (only the kernel starts programs); `write` is limited to 256 bytes per call; no dynamic libraries, no `brk`/`mmap`; errors while copying user pointers in `sys_open` are handled per byte instead of per page (slow, but correct).

This completes the sequence of stages 1–7 described in section 4. What is missing is the actual Ulix kernel from `ulix-book.nw` (see sections 1 and 9): thread table, `fork`/`exec`/`wait`, signals, semaphores, VFS, terminals, swap, copy-on-write, and the move back into the literate-programming document.

## 12. Inventory of the actual Ulix kernel (`ulix.c`, 8 897 lines)

Basis: `notangle -Rulix.c` from the book, then functions counted (269 functions, 4 800 lines; the remaining ~4 100 lines are structures, constants, macros, globals and tables) and the code compiled as a test with `aarch64-elf-gcc -fsyntax-only -Wall -Wextra`. The classification below is **heuristic** (a search for typical x86 names), not a manual check of every function.

**Good news:** the kernel code compiles for AArch64 **without a single error** (the compiler does not check the 31 inline assembly blocks in `-fsyntax-only` mode; they have to be replaced by hand anyway). 205 warnings remain, of which 153 are real 64-bit problems (97× integer → pointer, 56× pointer → integer) and 40× `pointer-sign`.

### Causes of the 64-bit problems

- Almost everything goes back to **one** definition: `typedef unsigned int memaddress;` (line 584). Changed to `unsigned long`, most of the warnings disappear. In addition `size_t` (currently `int`).
- 63 of the 153 hits are in macros and globals outside functions; the rest is spread over 53 places (for example `syscall_open` 13×, `ksh_command_test` 9×).
- `ulong`/`unsigned long` are used in a few places as a 32-bit value (signal masks, `unixtime`, GDT/IDT helpers). On AArch64 they are 64 bits wide; that does not matter for the mask fields, but on-disk structures would have to be checked (the Minix structures from stage 6 are already implemented with fixed widths).

### What belongs where (function lines)

| Area | Fn. | Lines | Assessment |
|---|---|---|---|
| Minix file system (`mx_*`) | 30 | 743 | can be taken over almost unchanged; connected to the disk only through `blk_read/blk_write` |
| System call bodies (`u_*`) | 35 | 660 | mostly portable; `u_fork` and `u_kill` need address space/frame adaptation |
| System calls (`syscall_*`) | 36 | 533 | dispatcher and register access new (`regs->eax` → `frame->x[0]`), bodies mostly reusable |
| Kernel shell (`ksh_*`) | 10 | 351 | portable, depends on console and keyboard |
| Address spaces/paging | 19 | 417 | **rewrite** on `map_page_in`/`free_user_space` (stages 4/5): 2-level x86 paging, swap (`page_out`) and copy-on-write must go to 4-level tables |
| Threads/scheduler | 11 | 262 | logic reusable, context switch and stack frames new (stage 5 has the building blocks) |
| Console/VT/keyboard | 26 | 411 | **replace** by UART (ten virtual terminals and VGA text go away or are mapped onto a serial console) |
| Floppy/IDE/disk | 22 | 398 | **goes away** (virtio-blk from stage 6 replaces it) |
| Interrupts/timer/clock (GDT/IDT/PIC/TSS) | 9 | 164 | **goes away** (stage 3), `unixtime` stays; the clock comes from the PL031 instead of CMOS |
| Rest (pipes, signals, helper functions) | 71 | 861 | mostly portable; signal delivery (return trampoline, manipulating the frame) is x86 specific |

Roughly: about 55 % of the function lines contain nothing architecture specific and only need to be recompiled with the new `memaddress`. Around 35 % touch x86 registers, segments or paging and have to be adapted. About 10 % are hardware drivers that are replaced by the ones already written.

### User side

- `lib-build/`: `ulixlib.c` (570 lines), `init.c`, `orig-sh.c` (824 lines, the shell) and about 60 small programs in `lib-build/tools/`. So far they are built with `i586-elf-gcc` and call the kernel through `int 0x80`. For AArch64 the system call stubs have to be changed to `svc #0` (number in `x8`) and everything built with the new linker script `user.ld` (the format has been there since stage 7).
- The system call **numbers** and structures (for example `getpsinfo`, `stat`) must stay identical between kernel and library; they come from the same book, which helps.

### Realistic order for the port

1. `memaddress` to 64 bit, bring the kernel code into `arm64/`, at first only the portable areas (Minix including writing, `dev_*`, pipes, signal structures). Goal: the file system tests from the book run on the virtio disk.
2. Process/thread table (TCB) and scheduler from the book instead of the simplified ones from stage 5; `fork` with address space copy (`as_*`) on the new page tables.
3. System call dispatcher on `svc` with the system call table from the book; signals; semaphores/mutexes.
4. Console: UART instead of VGA/keyboard, `kernel_shell` on top of it.
5. Rebuild user space (`ulixlib`, `init`, `sh`, tools) and start `init` from the disk.
6. Optional parts: swap and copy-on-write on 4 KB pages, several terminals.
7. Result back into `ulix-book.nw` as a literate program (new chapters for ARM64) and documentation.

**Assessment:** it is less than feared, but not little. The hardware layer (stages 1–7) is done; the portable core is manageable and already compiles. The effort lies in the roughly 1 600 lines that relate to x86 paging, registers and process context, in user space, and in debugging how it all works together. I cannot give a serious number of hours; in size it is about as much as the previous seven stages together, and the most difficult places (`fork`, swap, signals) are still to come.

## 13. Step 1 of the kernel port: book code runs on ARM64 (reading and writing the file system)

**Constraint: the x86 32-bit build is preserved.** All changes to the book (`ulix-book.nw`) are therefore architecture neutral or separated with `#if defined(__i386__)`. Evidence after every change to the book: the `.text` of the x86 object file is **bit-identical** to the baseline (same MD5 of the `.text` section, same sizes of text/data/bss), and the x86 kernel boots in `qemu-system-i386` with identical output.

Changes to the book:
- `memaddress` is now `__UINTPTR_TYPE__` (x86: `unsigned int`, AArch64: `unsigned long`), `size_t` is `__PTRDIFF_TYPE__` (x86: `int`, AArch64: `long`). This removes most of the 153 pointer/integer warnings from section 12; in the code taken over, zero cast warnings remain.
- `PHYSICAL(x)` (RAM window) has the base `0xFFFF000000000000` on AArch64.
- The call of the custom system call `readchar` in `u_read` is now in the macro `READCHAR_SYSCALL`: x86 expands to the same assembly as before, other architectures call `arch_readchar()`.

New in `arm64/` (the kernel code of the book is **not copied**, but generated anew from the book on every build):
- `ulix-arm64.nw`: noweb root chunks that reuse the chunks of the book (constants, macros, types, prototypes, global variables, functions): `notangle -Rulix-decl.h ../ulix-book.nw ulix-arm64.nw`.
- `tools/port.py` + `port.manifest`: select individual functions and globals; currently 77 functions and 14 globals: the complete Minix file system `mx_*`, the helper functions (`splitpath`, `relpath_to_abspath`, string/memory functions), the block layer (`readblock`, `writeblock_raw`) and the device/VFS layer above it (`u_open`, `u_read`, `u_stat`, `u_close`, `u_unlink`, `u_chdir`, `u_getcwd`, `dev_*`, `check_access`, `get_dev_and_path`).
- `glue.c`: platform dependent replacement: `readblock_hd/writeblock_hd` over virtio-blk, `dev_size`, stubs for floppy and serial disk, `arch_readchar` over the UART.
- **Known gap:** the buffer cache of the book is not yet ported (it needs the thread table and mutexes). `writeblock` in `glue.c` therefore writes straight through to the disk (the book only stores into the cache and writes later). As soon as the scheduler of the book runs, the `glue.c` stubs and `writeblock` go away again in favour of the book versions (only `port.manifest` changes).
- `bookfs_test.c`: tests of the book code on the virtio disk: reading the x86 files (size, checksum), create/write/read/overwrite (40 000 bytes over the indirect zone), `lseek`, `ftruncate`, `unlink`, 300 000 bytes over the double indirect zone, `mkdir` (also nested), hard links with link count, and the block balance: after deleting all helper files, the number of free blocks matches exactly the need of the remaining files.
- `tools/minix_check.py` and `make verify`: the test runs on a copy of the image **without** `-snapshot`; afterwards an independent Python program checks the modified image (directory tree, inode and zone bitmaps, link counts, orphaned zones/inodes) and compares the contents of `/keep.bin` byte by byte with the expected pattern. Result: 0 problems, 0 orphaned zones or inodes. The script also checked the helper program `minix_add.py` and the original x86 image (both fine).
- `minix_add.py --grow=N` enlarges the test image (file system from 1440 to 4096 zones = 4 MByte) so that write tests have room; `disk.img` is thereby 4 MByte large.

Result in qemu (Cortex-A72, EL1 and EL2, legacy and modern virtio): **79 checks, all pass**, including the tests of stages 4 to 7.

Two peculiarities of the book that I learned while testing (not porting errors): `mx_getdent` also returns unused 32-byte slots (inode 0), and `mx_mkdir` ([line 23942](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L23942)) does not check whether the directory already exists (`u_mkdir` does that one level up); a second `mx_mkdir` on an existing path would damage the file system.

Next step (step 2 from section 12): thread table (TCB) and scheduler of the book instead of the simplified ones from stage 5; then the buffer cache stubs and the write-through `writeblock` go away.

## 14. Step 2 of the kernel port: the thread code of the book runs on ARM64

From the book, **the scheduler, ready and blocked queues, `block`/`deblock`, mutexes (`mutex_lock/unlock`, `get_new_lock`) and kernel semaphores** now also run on ARM64 (98 functions and 23 globals in the selection altogether). The kernel threads switch through the context switch from stage 5.

Changes to the book (all x86 neutral, checked):
- `<<scheduler implementation>>`: the context switch (ESP/EBP, CR3, TSS, `iret` for new threads) as well as the blocks "check signals" and "free old kernel stacks" are now in `#if defined(__i386__)`; otherwise the scheduler calls `arch_context_switch()`.
- `<<resign>>`: x86 keeps `int 0x80` (system call 66), other architectures call `arch_resign()`.
- Prototypes of `arch_resign` and `arch_context_switch` under `#if !defined(__i386__)`.
- **Bug fix in the book:** `wait_semaphore` and `signal_semaphore` worked on a **copy** of the semaphore (`kl_semaphore sem = kl_semaphore_table[sid]`; place in the original: [line 18163](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18163-L18164) and [line 18186](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L18186-L18187), likewise in the inactive variant [lines 19413–19426](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L19413-L19426)). Counter and queue were only changed in the copy on the stack, so the semaphores never worked on x86 (the kernel uses them nowhere, only the mutexes, which is why nobody noticed). Now a pointer is used, and the text has a remark about it.

Evidence that x86 stays unchanged: after the scheduler/`resign` changes the `.text` section is still bit-identical. After the semaphore fix, exactly the two corrected functions differ out of 353 (comparison of the disassembly with normalised addresses), and the x86 kernel boots in `qemu-system-i386` with identical output.

New in `arm64/`:
- `arch_thread.c`: platform side of the threads. Every thread has its own kernel stack (4 pages); `arch_context_switch` calls `cpu_switch` (callee-saved registers, SP, `SP_EL0`). `arch_threads_init` makes the boot thread the idle thread 1 of the ready queue, `arch_thread_create` starts kernel threads (parent ID 1, so that the scheduler cleans up zombies itself), `arch_threads_reap` frees the stacks of finished threads. As on x86, kernel code runs with interrupts masked, which makes the queues of the book safe without further locks; the timer only switches threads when it meets the idle thread in `wfi` (later also in user mode).
- `thread_test.c`: 3 threads increment a counter under a mutex and give up the CPU **inside** the critical section (exactly 450 expected, never more than one thread in the section, 1352 context switches); producer/consumer with two semaphores over a 4-slot buffer (60 elements, order and sum correct); a thread that waits for a semaphore and is woken by another; zombie cleanup and frame balance.
- `tools/port.py` additionally generates `gen/ulix-api.h` with `extern` declarations of all selected globals, so that the platform files can access the book variables.

Result in qemu (EL1 and EL2): **91 checks, all pass**; `make verify` (file system write tests on the image, host check) still without problems.

Known gaps: the buffer cache of the book is not yet included (the write-through in `glue.c` stays); switching between **address spaces** (TTBR0/ASID in `arch_context_switch`) is missing, because all test threads run in the kernel address space; signals and the cleanup code for kernel stacks from the scheduler do not exist on ARM64 yet. The simple process management from stage 5 keeps running alongside (the timer chooses which scheduler is active), until `fork`/`exec` from the book (step 3) replace it.

## 15. Step 3 of the kernel port: system calls, fork, exec, wait on ARM64

The system call dispatcher of the book (`syscall_handler`, `install_syscall_handler`) and the system calls `exit`, `fork`, `read`, `write`, `open`, `close`, `waitpid`, `execve`, `unlink`, `chdir`, `lseek`, `getpid`, `getppid`, `gettid`, `resign`, `stat`, `getcwd`, `get_errno` now run on ARM64. The first, simplified process management from stage 5 (`sched.c`, hand-written assembly programs, its own system call layer) is **removed**; its tests now run in a new form over the code of the book.

**System call ABI** (mirrors the x86 convention of the book): `svc #0`, system call number in `x0` and the result also in `x0`, arguments in `x1`–`x5`. The numbers are the `__NR_*` of the book. The ARM64 `context_t` in the book carries the x86 register names as aliases (`eax`=`x0`, `ebx`=`x1`, …, `eip`=`ELR`, `useresp`=`SP_EL0`), so `r->ebx`, `eax_return(...)` and `r->eax = …` in the book run unchanged. For that, exception entry now also saves `SP_EL0` in the frame (288 bytes).

New in `arm64/arch_thread.c` (platform dependent replacement for what the book does for x86 with 2-level paging and kernel stacks inside the user address space, but with the same names and data structures):
- Address spaces: `address_spaces[]` of the book, plus the TTBR0 root per address space; `destroy_address_space` (leaves the address space first), switch in the context switch hook (TLB flush on every switch; ASIDs would come later).
- `u_fork`: copies all pages of the parent (like the book, without copy-on-write), copies the TCB, reopens files (`u_reopen`), creates a kernel stack for the child with a copy of the exception frame (`fork` returns 0 there), enters it into the ready queue.
- `u_execv`: copies the arguments into the kernel, loads the ELF64 program into new tables (the loader from stage 7), builds `[argc][argv]` on the new stack, switches over and frees the old address space; on errors the old one is kept.
- `arch_start_user`: starts the first process from a file (the kernel plays the parent; parent process 1 = idle, so that the scheduler disposes of it without `waitpid`).
- Errors in user code (for example writing to the code page) end the process through the book's `syscall_exit` with status -1.

Change to the book (all x86 neutral): `context_t` for ARM64 (next to the x86 type in `#if`).

User side (`arm64/user/`): `ulib.h` uses the ABI of the book and now has `fork`, `waitpid`, `execv`, `getppid`, `lseek`, `yield`; `crt0.S` reads `argc/argv` from the stack; new test program `forktest`.

Tests in qemu (EL1 and EL2, modern virtio): **89 checks in the kernel plus 25 inside the user programs, all pass.** New: `fork` with separate address spaces (writing in the child does not change the parent), exit status through `waitpid`, `fork`+`exec` of `/hello64` with arguments, three children run alternately, a crashing child is ended and the parent keeps running (status -1), `execv` of a missing file returns -1, grandchild process, no frame lost. `make verify` (host check of the image) still 0 problems.

x86 evidence: of 353 functions in the x86 object, still only the two deliberately corrected semaphore functions differ; the x86 kernel boots in `qemu-system-i386` with identical output.

Open / known gaps:
- No copy-on-write and no `brk`/`sbrk` (heap) for user programs; `write` always returns -1 because of a bug in the book (`u_write` falls into `default: return -1` after the output to STDOUT/STDERR, place [lines 20713–20731](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20713-L20731)); the output itself works.
- The book dereferences user pointers directly (as on x86). On ARM64 that works because TTBR0 is active, but an invalid pointer in a system call would halt the kernel.
- Signals, pipes, pthreads, terminals and the kernel shell are still missing; the buffer cache is still not included.

## 16. Step 4 of the kernel port: signals (and: there are no pipes)

**Pipes:** a search of the whole repository showed that x86 Ulix **does not use pipes**: there is no `pipe()` in the kernel (the book itself writes "pipes have not been implemented in this version"), no tool source with pipes, and no `|` in the shell source (`lib-build/tools/sh.c`). The shell can redirect (`<`, `>`, `>>`, `2>`), and does so with `close(1)` + `open(file)` (the new descriptor takes the free slot), that is without `dup2`; of `dup2` only the number 63 exists, no handler is installed. This runs with the file system and VFS code that is already ported. Pipes are therefore not rebuilt (a new function without a user).

**Signals** from the book (`u_signal`, `u_kill`, `syscall_signal`, `syscall_kill`: default action, `SIG_IGN`, `SIGSTOP`/`SIGCONT`/`SIGKILL`, pending bits) run unchanged. Only the delivery to user code is new on ARM64 (in the book it sits in the scheduler and manipulates the x86 stack):
- `arch_signal_check`: called at the end of every exception from user mode (system call and timer interrupt). It saves the **whole** interrupted register frame on the user stack, calls the handler like a C function (`x0` = signal number) and sets the link register to a trampoline page.
- Trampoline page (`0x7FFF0000`, in every address space): `movz x0,#600; svc #0`. The new system call `sigreturn` (number 600, ARM64 only) restores the saved frame; status register and return address are checked in the process (the process stays in EL0).
- When a new process enters user mode for the first time, this is checked as well: a signal that arrived before the first run is delivered there. I found the bug (a signal stayed pending forever because a fast child never had an exception) through a test.
- A process ended by another process with `SIGKILL` never runs through `syscall_exit`; its address space is now freed when the kernel stacks are cleaned up (no memory loss).

Change to the book: `eax_return` casts to `memaddress` instead of `unsigned int` (identical on x86; on ARM64 pointers and negative values are preserved; it had shown two real 64-bit warnings in `syscall_signal`).

Test (`user/sigtest.c`, all 8 checks run inside the user program): a handler interrupts a computation with many registers for 8 million loop iterations, the result is still exact, and the handler ran several times (before the first instruction and in the middle of the computation); `SIG_IGN` does not interfere; without a handler `SIGTERM` kills the process (message of the book); `SIGSTOP` stops a process (no progress in a log file), `SIGCONT` continues, `SIGKILL` ends it immediately. Result in qemu (EL1 and EL2, modern virtio): **93 checks in the kernel, all pass**; `make verify` without problems. x86: of 353 functions still only the two corrected semaphore functions differ, and the x86 kernel boots with identical output.

Known peculiarities of the book that I did not change (`u_kill`: [line 27781](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L27781)): `kill` on a blocked parent process leaves its children, which end later, as zombies (`u_kill` does not re-parent children); `SIGKILL` sets `used = false` immediately, and a following `waitpid` reports -1.

## 17. Step 5 of the kernel port: console over the UART and kernel shell

The console now runs over the PL011 UART. Everything behind the input buffer is code of the book: `syscall_readchar` (blocks the process in `keyboard_queue`), `kgetch`, `kreadline`, `kernel_shell`, `ksh_run_command` and the shell commands `uname`, `ps`, `queues`, `locks`, `lsof`, `inode`, `time`, `listas`, `longhelp`, `help`, `enable`/`disable`.

New in `arm64/`:
- `arch_console.c`: the UART receive interrupt (GIC number 33) replaces the keyboard interrupt of the x86 kernel. It converts characters (Enter → `\n`, DEL → `\b`), writes them into the input buffer of the terminal (`terminals[cur_vt]`) and wakes waiting processes, like `keyboard_handler` in the book. **Ctrl-C** ends the foreground process (the same search as in the book, `u_kill(..., SIGKILL)`), **Ctrl-]** stops the scheduler and starts the kernel shell (x86: Shift-Esc). The initialisation of the buffers comes directly from the chunk `<<setup keyboard>>` of the book (`ulix-arm64.nw` has a root of its own for this, `ulix-init.c`).
- `glue.c`: replacement for commands that concern x86 hardware (`mem`, `test`, `div0`, `hexdump` on a kernel address, `gf`/`gp` over the ARM64 frame allocator), `vt_clrscr` as an ANSI sequence; `arch_readchar` now calls `syscall_readchar` (blocking) instead of polling the UART.
- `kmain.c`: a single formatter for `printf` and `sprintf` (additionally `-` for left alignment, width for strings, `%p`); second kernel image `ulix-console.elf` (`-DCONSOLE_MODE`): starts `/contest64` and waits for input.
- `irq.c`: interrupts are now acknowledged **before** they are handled, so that the kernel shell can receive further characters inside the UART interrupt.
- `tools/console_test.py` + `make console-test`: plays a terminal. It starts qemu with the UART on a TCP port, types characters one at a time and checks the answers: enter a line and get it back by echo, backspace, Ctrl-C (`Killed`), then Ctrl-], kernel shell with all the commands above, an unknown command, `exit` and no kernel error. 21 checks pass.

Changes to the book (all unchanged in behaviour for x86):
- `<<enable interrupts>>` and `<<disable interrupts>>` are processor dependent (`sti`/`cli` on x86, `msr daifclr/daifset` on ARM64); `hexdump` and two calls take `memaddress` instead of `uint` (addresses stay complete at 64 bit).
- **Three bugs in the book fixed** (found through tests, each with evidence):
  1. `u_open` and `u_chdir` call `u_stat` and ignore the result; if the file does not exist yet (for example with `O_CREAT`), `struct stat` stayed uninitialised, and the stack contents decided whether the file is treated like a symlink. The result was, for example, that `open("/progress.txt", O_CREAT)` opened the root directory. Now `struct stat st = { 0 }`. Places in the original: `u_open` [lines 20519–20521](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L20519-L20521), `u_chdir` [lines 21444–21445](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L21444-L21445). (The symptom only appeared after an additional program file had changed the stack contents: a bug that can lurk on x86 just the same.)
  2. `mx_open` took a **closed** entry of the internal inode table for "already open" if its inode number matched (`mx_close` does not clear `ino`/`device`). When a new file got the number of a deleted one, it inherited its old size and zones. Now only an entry with `refcount > 0` counts. Place in the original: [lines 22860–22861](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L22860-L22861). Test in `bookfs_test.c`: without the fix "... starts with size 0" fails, with it the test passes.
  3. (from step 2) semaphores worked on a copy.

Result in qemu (EL1 and EL2, legacy and modern virtio): **95 checks in the kernel, all pass**; `make verify` 0 problems; `make console-test` 21 checks. x86: of 353 functions in the object, exactly the deliberately changed ones differ (`hexdump`, `mx_open`, `u_open`, `u_chdir` and the two semaphore functions); the x86 kernel boots with identical output.

Open: no recognition of escape sequences (arrow keys), no several virtual terminals (only terminal 0; the rest of the VT logic of the book is dropped), the kernel shell shows no status line; the buffer cache is still missing.

## 18. Step 6 of the kernel port: the user space of the book runs (init, login, shell, 60 programs)

The system now boots like the x86 original: `/init` → `/bin/login` (automatic login on terminals 1 and 2, login with password on the others) → `/bin/sh`, and the programs from `lib-build/tools` run as AArch64 binaries.

**Structure (`arm64/userland/`):** `ulixlib` and `init` are generated from the book on every build, the 59 programs from `lib-build/tools/*.c` are compiled unchanged with `aarch64-elf-gcc` (`recurse.c` contains x86 assembly and is dropped; for `swapper` a small placeholder replaces the source, there is no swap), together with `init` 60 programs. The linker script and the start code are those from stage 7. `make` builds the image `disk.img` from them: the ARM64 programs replace the x86 programs in `/bin` and `/init` (blocks of the old files are freed, `tools/minix_add.py` can now handle directories and replacing). A second kernel image `ulix-shell.elf` starts `/init`.

**Changes to the book** (x86 library `.text` bit-identical, kernel only at the places that fix bugs):
- The four system call functions of the user library (`syscall1`..`syscall4`) have an ARM64 branch (`svc #0`, number in `x0`, arguments `x1`..`x5`).
- `printf`/`sprintf` of the library read their arguments directly from the stack (x86 calling convention; place in the original: [lines 29393–29399](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L29393-L29399)); through macros (`PRINT_NEXT_INT` etc.) ARM64 now uses `va_arg`, and x86 keeps the same expressions.
- **Further bugs in the book fixed** (each noticed while testing):
  1. `syscall_waitpid` wrote through the status pointer even when it was 0 (`login` calls `waitpid (pid, 0, 0)`; place in the original: [line 11713](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11713) and [11729](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L11729)). On x86 that silently overwrites the first bytes of the program at address 0; on ARM64 the kernel crashed.
  2. `mx_rmdir` switched to the **parent** directory and deleted `.` and `..` there instead of in the directory to be removed (place in the original: [lines 24016–24022](https://github.com/hgesser/ulix/blob/32535a19a1a25bdd2b52e269de52bc1930c73fbb/ulix-book.nw#L24016-L24022)). Afterwards the parent directory was empty. New test in `bookfs_test.c`.

**Adaptations to the one console:**
- Ten virtual terminals of the book → one UART: the output of processes on a terminal that is not visible is buffered per terminal (4 KByte) and printed when switching. **Ctrl-T and a digit** switches (x86: Alt-1..0); only terminal 1 gets input interrupts assigned, the input buffers are per terminal.
- The mount table of the book mounts three disks (`hda`, `hdb`, `fd1`); here there is one, `/tmp` and `/mnt` are normal directories (`/tmp` with mode 1777; on x86 it is a disk of its own).
- Additional system calls: `set_errno`, `getpsinfo`, `setpsname`, `isatty`, `mkdir`, `rmdir`, `readdir`, `truncate`, `ftruncate`, `link`, `symlink`, `readlink`, `diskfree`, `sync`, `setuid/setgid/seteuid/setegid`, `login`, `query_ids`, `chown`, `chmod`, `brk` (new heap management for 4-level tables), `setterm`, `idle` (`wfi`), and placeholders for the VGA screen (`clrscr` through ANSI).
- argv strings now sit 512 bytes below the end of the stack (as on x86), because `ls` appends characters to its argument.

**Tests** (`make shell-test`, `make console-test`, `make verify`, kernel tests): the shell test runs a complete session over the UART: boot, auto login, `ls`, `cat`, `cd`, `touch`, `cp`, `wc`, `grep`, `stat`, `mkdir`, `rm`, `rmdir`, redirection with `>`, `ps`, `df`, `free`, `echo` with Ctrl-C, switch to terminal 2 (root shell, `ps`) and back, `exit`, login again with a password. 61 checks in the shell test, 100 kernel tests (EL1 and EL2), 21 in the console test, `make verify` 0 problems. The expected values for file sizes and checksums come at build time from the independent Python parser (`gen/disk_expected.h`).

x86: of 353 functions in the kernel object, only `hexdump`, `mx_open`, `mx_rmdir`, `syscall_waitpid`, `u_chdir`, `u_open` and the two semaphore functions differ (all corrections); the x86 kernel boots with identical output, the `.text` section of the x86 library is unchanged.

**Open:** no pthreads (`thread`, `mutex` tools do not run), no swap, no VGA functions (`vi`, cursor positioning), `recurse` (x86 assembly); the `ls` tool shows no times, because the clock is not read from the PL031 (timestamp 0).

## 19. Step 7: clock, copy-on-write, one command for all tests, documentation

- **Clock** (`arch_clock.c`): `system_start_time` comes from the PL031 RTC of the board, `system_ticks`/`system_time` are advanced like in the timer handler of the book (100 Hz). Files now get real timestamps (`ls -l`, `stat`).
- **Copy-on-write for `fork`** (`mmu.c`, `arch_thread.c`, `irq.c`): writable pages are made read-only in parent and child and marked with a software bit (bit 55); every frame has a reference count. The first write causes a permission fault; `cow_fault` copies the page (or makes it writable again if the frame has only one user left). The case "the kernel writes into a user buffer inside a system call" (`read`, `waitpid`) goes through the same error handling in EL1, the instruction is repeated; `user_to_phys` resolves COW as well. Code pages (read-only) are shared permanently. The test `user/cowtest.c` counts free frames: `fork` copies no data pages, reading copies nothing, writing copies exactly the 64 affected pages, the parent keeps its data, after the end of the child all frames are back (10 checks).
- **`make check`** runs everything: kernel tests (EL1 with legacy virtio, EL2 with modern virtio, 104 checks each), `make verify` (write tests plus independent check of the image), `make console-test` (21), `make shell-test` (63). Duration about 2 minutes. (State of this stage; the current numbers are in `arm64/README.md`.) `make run-shell` starts the system for interactive use.
- **Documentation:** `arm64/README.md` (English, in the style of the repository), a section in the top-level `README.md`, this report.

**Deliberately not done (state of this stage; buffer cache, pthreads, swap and the chapter in the book have been done since, see section 21):**
- **Buffer cache of the book:** it delays writes (a flush thread writes later). The tests that check the image from outside assume that things have been written; the gain (fewer disk accesses) is small under qemu. `writeblock` writes through.
- **Pthreads** (`u_pthread_create` builds x86 stacks): would need an ARM64 design for several threads per address space (shared TTBR0 table, separate kernel and user stacks, reference count on the address space).
- **Swap** (`page_out`, swapper process): presupposes the x86 frame table and its page replacement; with COW and 128 MByte of RAM without practical use.
- **Moving it back into the book as a chapter:** `ulix-arm64.nw` is already a commented noweb file, but a chapter of its own in `ulix-book.nw` would be a larger intervention in the TeX text, which I could not check without a LaTeX installation (`make pdf` is not possible here).
- **Real hardware:** GICv3, device tree, cache maintenance for DMA, other boards. (Written since and tested in qemu, not tried on real hardware.)

**Count of the size** (originally after step 7, updated on 2026-11-04, without the commit of the serial disk): 31 commits on the branch `arm64-port` since the upstream state `32535a1`; in the book 361 lines added and 51 removed (including the appendix about the port, otherwise x86 neutral or bug fixes); in the directory `arm64/` about 4 700 lines of C, assembly and headers (without the parts generated from the book), plus about 1 000 lines of Python and shell in `arm64/tools/` and 700 lines of test programs in `arm64/user/`.

## 19a. Addendum: the serial disk (2026-11-04)

Besides IDE and floppy, the book has a third disk, the "serial hard disk" (`/dev/sdisk`): a computer at the other end of a serial line plays the disk. It is rebuilt on ARM64 without changing the book.

- `readblock_serial` / `writeblock_serial` in `glue.c` speak the protocol of the book (CMD_GET / CMD_PUT, the block number as 4 bytes, 1024 bytes of data) over a second PL011 (`plat.uart2`: the second `pl011` node of the device tree; qemu creates it when it gets a second `-serial`). A reading process sleeps in `serial_disk_queue` until the receive interrupt has collected the block; the idle thread polls, because it must never sleep.
- The other end is `tools/serial_disk.py` (serves an image over qemu's TCP port for the serial interface). `make sdisk-test` (also the job `sdisk` in `make pcheck`) runs the kernel tests with it: block I/O, the Minix code of the book on the device `0x2a00` (`mx_stat`, `mx_read`, `mx_open`/`mx_write`), the buffer cache, two threads reading at the same time (sleeping and waking by the interrupt), and afterwards a check of the image on the host with `minix_check.py`.
- Nothing in the book uses `/dev/sdisk` (the device table says the device file does not exist); the disk is therefore neither mounted nor does it have a device file here.
- qemu's PL011 has no receive timeout, so the port runs with the FIFOs off (one interrupt per byte). The baud rate of this line is not set (qemu ignores it; a real UART would need it).

## 20. Network, backported from the branch `rpi4` (2026-11-08)

The network stack was developed on the branch `rpi4` (there in section 21 of this file, together with the preparation for the Raspberry Pi 4B). It does not depend on the Pi hardware and runs in qemu `virt` with virtio-net; that is why it is now also on `arm64-port`. On `rpi4` the port is a literate program (`arm64/lit/*.nw`); here the sources remain ordinary commented C files in `arm64/`. The code is the same: the files come from the state of `rpi4` before the conversion to literate programming (`f9e9c50`), with a few changes: the GENET driver is missing from the list of drivers (`netdev.c`), and the Pi remarks in the comments are gone.

**What was taken over:**

- `virtionet.c` (the network card of qemu `virt`: two queues), `netdev.c` (which card is there), `net.h` (what the layers offer each other), `net.c` (Ethernet, ARP, IPv4 without fragments, ICMP echo, UDP), `tcp.c` (connection setup and teardown, data in both directions, retransmission with a timer and growing waiting time, window of the receiver; no congestion control, out-of-order segments are dropped), `cache.h` (cache maintenance for DMA).
- DHCP client (`dhcp.c`), settings from the kernel command line (`netconf.c`: `ip=dhcp`, `ip=10.0.0.5/24`, `gw=`, `dns=`, `netecho`). For that, `dt.c` now reads `/chosen/bootargs` (with qemu: `-append`) and offers `plat_cmdline ()`.
- The network thread `netd.c`: sleeps in a queue and is woken by the interrupt of the card (`irq.c`), and by the timer every 50 ms for the timers of TCP; with `netecho` on the command line it serves the echo service (`echo.c`, port 7). `arch_thread_create_high` (`arch_thread.c`) puts it on one of the highest thread numbers, so that the low ones stay for the processes (the book expects `/bin/swapper` as process 2).
- System calls 602 to 611 (`net_syscalls.c`, `arch_thread.c`, `userland/arm64-extra.h`): raw ICMP sockets, time in microseconds, reading and changing the network settings (root only: `euid == 0`), DHCP, plus `poweroff` and `reboot` (buffer cache to the disks, then PSCI `SYSTEM_OFF` or `SYSTEM_RESET`; `smp_poweroff`/`smp_reboot` in `smp.c`).
- The programs in `/bin`: `ping`, `ifconfig`, `route`, `dhclient` (modelled on the BSD programs; the interface is called `vioif0` as in NetBSD) and `shutdown` (with the names `poweroff` and `reboot`). `/bin` thereby has 70 instead of 63 entries (`bookfs_test.c`, `kmain.c`). `make run-shell` starts qemu with a network card: `ping 10.0.2.2`, `ifconfig`.
- The book (`ulix-book.nw`) is changed only in the appendix (new section "The Network", the serial disk, the list of differences); the x86 kernel stays as it was.

**What was not taken over** (stays on `rpi4`): everything that exists only for the board: SD card (`sdcard.c`, `make_sdcard.py`), the GENET driver, device tree with `ranges` and the file `rpi4-qemu.dts`, `uart_init`, `maxcpus=`, the boot images for the Pi, and the literate-programming scaffolding (`lit/`).

**Tests:** `make pcheck` passes with 26 jobs in 238 seconds (all kernel tests with 129, 132 and 134 checks, the shell sessions, the console test, the file system check and the jobs `net`, `netd`, `netconf`, `poweroff`, `shellnet` and `sdisk`). The new tests in detail: `make net-test` (the kernel with its own image `ulix-net.bin`, without disk and processes, talks through qemu's user network with ordinary Python sockets: ping of the router, UDP echo, TCP echo with 100 and 20 000 bytes, and the kernel as a TCP client in which the first SYN and the first data segment are lost on purpose and have to be sent again; plus the DHCP checks), `make netd-test` (the thread answers UDP and TCP echo while the kernel runs its program; the interrupt, not polling, wakes it), the job `shellnet` in the shell test (`ping`, `ifconfig`, `route`, `dhclient`; changing only as root) and the job `netconf` (command line with a fixed address and with `ip=dhcp`).

## 21. Open points (replaces `arm64/TODO.md`)

There is no `TODO.md` any more; the lasting limitations are in the "Not done" section of `arm64/README.md`, the rest is here.

- **Network:** UDP and TCP sockets for user programs (raw ICMP exists), ping from outside (the thread answers it, but qemu's user network cannot send one; a `-netdev dgram` peer could), renewing the DHCP lease, maybe DNS, if needed fast retransmit and congestion control in TCP; for `ifconfig` and `route` several interfaces (`-a`), aliases, `up`/`down` and routes other than the default route.
- **Real hardware:** everything ran only in qemu. For a real board the device tree reader, the cache maintenance for DMA and the PSCI start of the other CPUs are written, but not tried. The branch `rpi4` prepares the Raspberry Pi 4B (boot, SD card, GENET; the checklist for the day the board arrives is there in section 23).
- `/dev/fd0` has no drive (a fourth virtio disk would do; small).
- Differences from x86 that remain: `df` shows `/dev/hdb` and `/dev/fd1` with the numbers of `/dev/hda`; the swap file grows on demand instead of occupying 64 MByte in advance.
- Not checkable on this machine: the x86 build with the compiler that the makefiles name (`gcc-4.4`); all checks used GCC 16 (`i686-elf-gcc`). `ulixlib.c` still passes pointers as `uint` to the system call functions; that works because user addresses are below 2 GByte, and a change would also affect the x86 build. The whole book was not typeset on this side of the fork (`make pdf`); on `rpi4` it works with `tectonic`.
- The bugs of the book that are also on x86 and were not changed (`mx_ftruncate`, `mx_unlink` of large files, `u_write`, `mx_mkdir`, `u_kill`) are listed with their places in the original book in `arm64/README.md`. The swap file is therefore never shortened on the second disk.
- Publishing: `origin` is the original project (`hgesser/ulix`) and is never written to; backups go to the private repository `MatthewJer/ulix-arm64` (remote `backup`; not a GitHub fork, because a fork of a public repository cannot be private). Public only when the notices "not the work of the authors" (READMEs, this file, appendix of the book) have been checked once more; keep in mind that `bin-build/bigdisk.img` (98 MByte) is in the history of the original.
