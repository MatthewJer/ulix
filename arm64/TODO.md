# ARM64 port: open points

State: the port runs in `qemu-system-aarch64 -M virt` and passes `make check` (see `README.md`). This list is what
is still missing compared with the x86 build, most important first. Background and reasons: `../ARM64-PORT.md`
(sections 14-19).

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
      sequences (cursor positioning, colors, a scroll region of 24 lines). The book's `vi` works. Differences: there is no
      status line (line 25), and the console must understand ANSI sequences.
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
      - [ ] Create the fork under the user's account (`gh` is logged in as `MatthewJer`), set its description to the same notice,
        push `arm64-port`. Only after the user has agreed to the exact commands.
