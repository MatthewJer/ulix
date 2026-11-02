# ARM64 port: open points

State: the port runs in `qemu-system-aarch64 -M virt` and passes `make check` (see `README.md`). This list is what
is still missing compared with the x86 build, most important first. Background and reasons: `../ARM64-PORT.md`
(sections 14-19).

## Missing functionality

- [ ] **Pthreads** (13 functions: `u_pthread_create`, `syscall_pthread_*`, mutex calls for user threads).
      The programs `thread` and `mutex` in `lib-build/tools` do not run. Needs a design for several threads per
      address space: shared TTBR0 table, own user and kernel stack per thread, reference count on the address
      space (also for `exit` and `exec`), `top_of_thread_kstack` handling. Test: a user program with 3 threads
      and a mutex, like `thread_test.c` but in user mode.
- [ ] **Buffer cache of the book** (`buffer_read`, `buffer_write`, `buffer_sync`, the flush logic). Today
      `writeblock` writes through (`glue.c`). Needs: periodic flush (timer task or thread), `sync` at exit/shutdown,
      and the tests that look at the disk image from outside (`make verify`) must call `sync` first.
- [ ] **Swap** (`page_out`, swapper process, page replacement counters). Depends on a frame table with
      reference counts (exists since copy-on-write) and a reverse mapping from frame to page table entries.
      `/bin/swapper` is only a placeholder.
- [ ] **VGA-only functions**: `read_screen`, `write_screen`, `get_xy`, `set_xy`, cursor positioning. The tool
      `vi` does not work as an editor. Could be emulated with ANSI escape codes plus a screen copy in the kernel.
- [ ] **Kernel shell commands for x86 hardware**: `test`, `div0`, `rp` are placeholders. (`mem` now dumps the
      4-level tables of the current address space.)
- [ ] `recurse` (in `lib-build/tools`) contains x86 assembler and is skipped.

## Hardware and robustness

- [ ] **Real hardware / other boards**: GICv3 (qemu `-M virt,gic-version=3`), parse the device tree instead of
      the fixed 128 MByte of RAM and the fixed device addresses, cache maintenance for DMA in `virtio.c`
      (it relies on coherent memory), more than one CPU.
- [ ] **User pointers in system calls**: the book dereferences them directly (like on x86). A bad pointer inside a
      system call stops the kernel instead of ending the process. Idea: handle data aborts at EL1 on user
      addresses in `exc_handler` (the copy-on-write path already does this for write faults) and fail the system call.
- [x] Kernel stacks of finished processes are freed at the next `fork` (`arch_threads_reap`), and per-process
      ASIDs replace the TLB flush at address space switches (needs 16-bit ASIDs, else the old flush is used).
- [ ] Interrupt-driven virtio (`virtio.c` polls) and more than one request at a time.
- [ ] Output of a terminal that is not visible is kept in 4 KByte per terminal (`glue.c`); the x86 version keeps
      the whole screen.

## Book and documentation

- [ ] **Chapter about the ARM64 port in `ulix-book.nw`** (the code is documented in `ulix-arm64.nw` and
      `README.md`, but not yet in the book). Needs a LaTeX installation to check `make pdf`.
- [ ] Check the x86 build with the toolchain the Makefiles name (`gcc-4.4`, `i586-elf-gcc`); all checks so far used
      GCC 16 (`i686-elf-gcc`) and qemu.
- [ ] Further 64-bit clean-up in the book: `ulixlib.c` still passes pointers as `(uint)` to the system call
      functions (works because user addresses are below 2 GByte); the warnings are switched off in
      `userland/Makefile`.
- [ ] Open a pull request from the branch `arm64-port` (14 commits).
