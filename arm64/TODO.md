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
- [ ] **Kernel shell commands for x86 hardware**: `test`, `div0`, `rp` are placeholders. (`mem` now dumps the
      4-level tables of the current address space.)
- [ ] `recurse` (in `lib-build/tools`) contains x86 assembler and is skipped.

## Hardware and robustness

- [ ] **Real hardware / other boards**: GICv3 (qemu `-M virt,gic-version=3`), parse the device tree instead of
      the fixed 128 MByte of RAM and the fixed device addresses, cache maintenance for DMA in `virtio.c`
      (it relies on coherent memory), more than one CPU.
- [x] **User pointers in system calls**: like on the x86 (the book's page fault handler treats a fault inside
      kernel code as a kernel error) an exception in the kernel stops the scheduler and jumps to the kernel shell
      (`arch_kernel_fault_shell`). A bad pointer in a system call therefore does not end just the process on
      either platform. Test: `badptr64` in the shell test.
- [x] Kernel stacks of finished processes are freed at the next `fork` (`arch_threads_reap`), and per-process
      ASIDs replace the TLB flush at address space switches (needs 16-bit ASIDs, else the old flush is used).
- [ ] Interrupt-driven virtio (`virtio.c` polls) and more than one request at a time.
- [x] Output of a terminal that is not visible is kept as its screen (24 lines, like on the x86), not in a 4 KByte buffer.

## Book and documentation

- [ ] **Chapter about the ARM64 port in `ulix-book.nw`** (the code is documented in `ulix-arm64.nw` and
      `README.md`, but not yet in the book). Needs a LaTeX installation to check `make pdf`.
- [ ] Check the x86 build with the toolchain the Makefiles name (`gcc-4.4`, `i586-elf-gcc`); all checks so far used
      GCC 16 (`i686-elf-gcc`) and qemu.
- [ ] Further 64-bit clean-up in the book: `ulixlib.c` still passes pointers as `(uint)` to the system call
      functions (works because user addresses are below 2 GByte); the warnings are switched off in
      `userland/Makefile`.
- [ ] **Publish the branch `arm64-port` as a fork in the user's own GitHub repository** (no pull request to the
      original repository; nothing is pushed until the user says so; `origin` points to the original author's
      repository and must not be pushed to). Before that, document clearly that the ARM64 port is *not* the
      work of the original authors and that they have nothing to do with it (as of now):
      - a notice at the top of `arm64/README.md`, `ARM64-PORT.md` and the main `README.md` of the fork;
      - the repository description of the fork says the same;
      - do not suggest endorsement or affiliation (no "official" wording, commit messages and docs say who wrote what);
      - keep the original license and copyright notices (`LICENSE`) and check what they require for a derived work.
