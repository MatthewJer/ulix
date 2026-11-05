# ARM64 port: open points

State: the port runs in `qemu-system-aarch64 -M virt` and passes `make check` (see `README.md`). This list is what
is still missing compared with the x86 build, most important first. Background and reasons: `../ARM64-PORT.md`
(sections 14-19).

## Where we stopped (2026-11-05) -- start here: waiting for the Raspberry Pi

Work continues when the hardware has arrived (Pi 4B 1 GB, USB-C power supply, CP2102 UART module, 8 GB microSD, card reader,
Dupont cables). Everything that can be tested in qemu is done: on branch `rpi4` the kernel, the console test and the whole shell
session run on qemu's `raspi4b` machine from an SD card image (details in "Raspberry Pi 4B: progress" below), the SD card works with
interrupts, multi-block commands and four data lines, and the network (virtio-net, ARP, IPv4, ICMP, UDP, TCP) is tested against real
sockets of the host (`make net-test`, see "Network: progress"). Principles for
the rest of the work: `PRINCIPLES.md` (teaching system: simple, readable, small steps; BSD code only as a reference).
The branch `rpi4` is only merged into `arm64-port` when `make pcheck` is green.

### When the board arrives -- checklist, in this order

1. **Prepare the card.** Everything is ready except the firmware files, which need a download (ask first, name file, source and
   size): `start4.elf`, `fixup4.dat` and `bcm2711-rpi-4-b.dtb` from the `boot/` directory of github.com/raspberrypi/firmware. Put
   them into the directory `firmware/` (it is ignored by git), then `make sd-pi.img`: formats the first partition as FAT32 (mtools),
   copies the firmware, `rpi4-config.txt` (as `config.txt`: `arm_64bit=1`, `enable_uart=1`, `dtoverlay=disable-bt` to put the PL011
   on GPIO 14/15), `rpi4-cmdline.txt` (as `cmdline.txt`: `maxcpus=1`) and `kernel8-shell.img` (as `kernel8.img`). Tested here with
   dummy files. Write it to the card: `diskutil list`, `diskutil unmountDisk /dev/diskN`, `sudo dd if=sd-pi.img of=/dev/rdiskN bs=4m`.
   Check that the firmware is happy with the partition table that has our Minix partitions (type 0x81).
2. **Wire the UART** (3.3 V only!): adapter TXD to pin 10 (GPIO 15, RXD), adapter RXD to pin 8 (GPIO 14, TXD), GND to pin 6; no
   5 V. Terminal on the Mac at 115200 baud, 8N1 (`screen /dev/cu.usbserial-... 115200`).
3. **The baud rate** is set by the kernel (`uart_init` in `kmain.c`): if the divisor is still 0, 115200 baud for the 48 MHz clock
   of the Pi (IBRD 26, FBRD 3); a divisor that the firmware has set stays. FIFOs off. Check on the board that the text is readable;
   if not, the firmware's UART clock is different (`core_freq`, `init_uart_clock` in `config.txt`).
4. **First boot.** Expect surprises, look at the serial output first. Things already known to check:
   - The firmware starts the kernel at EL2 and passes the device tree in `x0` (`start.S` drops to EL1; `boot_dtb`). Save the tree
     the kernel gets and compare with `rpi4-qemu.dts` (`dtc -I dtb -O dts`); test `dt.c` with the real one, including `ranges`.
   - Alignment faults on device memory: 8- and 16-bit registers must be accessed with their own width (as in `sdcard.c`).
   - Only one CPU for now: the firmware parks the other three (spin table, no PSCI). `cmdline.txt` says `maxcpus=1`, which the kernel
     reads from `/chosen/bootargs` (`dt.c`; tested in `make pcheck` with qemu's `-append maxcpus=2`) and then starts no CPU via PSCI.
     Check that the firmware passes the command line. Later: start the other cores from the spin table.
   - The 1 GByte model has its peripherals in block 3 and RAM in block 0, which fits the boot tables; the 4 and 8 GByte models do not
     (see "Still open in step 1").
   - The generic timer frequency on the board is 54 MHz (qemu: 62.5 MHz): the code reads `cntfrq_el0`, check the 100 Hz tick.
5. **SD card on the real controller** (EMMC2 at 0xfe340000): see "Not done and not testable in qemu" in the Pi progress section. If the
   card does not initialise: log the status registers; the clock and power of EMMC2 may need the firmware mailbox
   (`bcm2835_mbox`, FreeBSD `bcm2835_mbox.c` as reference).
6. When the kernel tests, the console and the shell session run on the board: merge `rpi4` into `arm64-port` (after `make pcheck`),
   tag the state, update `README.md` ("Not done": real boards are tested), and write the log entry in `../ARM64-PORT.md`.

### After that (the actual goal: Ulix on the Pi with telnet)

7. **SD card, the rest:** interrupts, multi-block commands and the 4-bit bus are done (2026-11-05, tested in qemu). Left: DMA (ADMA2),
   the 1.8 V switch and faster clocks (UHS), and everything the real controller needs (see "Not done and not testable in qemu").
8. **Network, the rest:** the layers up to TCP are done (see "Network: progress"). Left: an interrupt for the virtio-net card instead of
   polling (the same pattern as the SD card: the process sleeps, the interrupt wakes it), a kernel thread that runs the network all the
   time (until now whoever waits calls `net_poll_once ()`; without it nobody answers when another computer pings us), UDP and TCP sockets
   for user programs (raw ICMP is done: system calls in the same style, then telnet can be a program), the answer to a ping that comes from
   outside (untested: qemu's user network cannot send one; a `-netdev dgram` peer could), renewing the DHCP lease before it runs out (DHCP itself is done), DNS maybe, and TCP fast retransmit/congestion control if they are ever wanted.
9. **GENET driver** for the Pi's Ethernet (BCM2711, `sys/arm64/broadcom/genet/if_genet.c` and `if_genetreg.h` of FreeBSD as the
   reference): register definitions, DMA rings, PHY (MDIO) setup, link detection. Only on the board (or on newer qemu if it
   gets a model). Plug it into the same interface as the virtio-net driver.
10. **Telnet server**: a daemon (kernel thread or user process) that listens on port 23, handles the minimal telnet protocol (IAC, echo,
    suppress go-ahead, terminal size can be ignored), and attaches the session to a virtual terminal of the book (there are ten).
    Login through the book's `/bin/login`. Test: a Python script that connects to the guest and runs commands.
11. **A clock over the network** (no RTC on the Pi): at the end a simple time query (SNTP over UDP) instead of the fixed start date
    of 1 January 2026.

### Smaller open points (not tied to the board)

- Ask-first rule for downloads (firmware files, mtools if missing, anything else): name file, source and size.
- The log entry for the Pi work so far is section 20 of `../ARM64-PORT.md`; add the results of the board there.
- The Pi image `kernel8.img` is the test kernel; `kernel8-shell.img` boots to the shell (`make sd-pi.img` uses that one).
- `virtio.c` and `virtionet.c` each define the virtio register offsets: move them into one `virtio.h`.
- Before the repository is made public: check the notices again (see "Then" below).

### Network: progress (2026-11-05, branch `rpi4`)

- `virtionet.c` (the card: two queues, polling), `net.c` (Ethernet, ARP with a table of 8, IPv4 without fragments, ICMP echo, UDP with
  bound ports), `tcp.c` (open, data both ways, close; retransmission with a timer and backoff, the receiver's window respected, out of
  order segments dropped; no congestion control), `net.h` (the interfaces), `net_test.c` (the kernel side of the test, its own
  kernel image `ulix-net.bin` without disk and processes).
- `make net-test` (`tools/net_test.py`, job `net` in `make pcheck`): qemu's user-mode network (the kernel is 10.0.2.15, the router and
  the host are 10.0.2.2) with port forwarding, so that ordinary Python sockets talk to the kernel: UDP echo, TCP echo of 100 and of
  20000 bytes (more than the buffers and than one segment), and the kernel as the client: connects to the host with its first SYN
  thrown away (`net_drop_tx`), sends a line whose first segment is thrown away, reads the answer, closes. Everything passes.
- Addresses are fixed (`net.c`: 10.0.2.15, router 10.0.2.2), which is what qemu's user network gives.
- **`ping` in the Ulix shell** (`userland/ping.c`, in `/bin`): `ping [-c count] [-s size] A.B.C.D`, dotted addresses only (no DNS). The
  program builds the ICMP messages itself, checksum included; the kernel offers raw ICMP sockets (`net.c`: `icmp_open/send/get`) through
  five system calls of the port (`net_syscalls.c`, numbers 602 to 606, used through `userland/arm64-extra.h`): `icmp_open`, `icmp_send`,
  `icmp_recv` (waits with a timeout and gives the CPU to others meanwhile), `icmp_close`, `clock_us`. Try it: `make run-shell`, then
  `ping 10.0.2.2` (qemu answers for its router; other addresses of the outside world may work, too). Shell test: job `shellnet`.
  The network is only served while a program waits for it (`ping` calls the card in its receive loop); there is no thread for it yet.
- **Configuration, in the manner of the BSDs** (2026-11-05): `dhcp.c` is a DHCP client (DISCOVER, OFFER, REQUEST, ACK; address, netmask,
  router, name server, lease time; no renewal); `netconf.c` reads the kernel command line (`ip=dhcp`, `ip=10.0.0.5/24`, `gw=`, `dns=`;
  on the Pi that is `cmdline.txt`, which now says `maxcpus=1 ip=dhcp`; qemu: `-append`). Programs, all in `/bin`: `ifconfig`
  (show; set with `ifconfig vioif0 inet 10.0.2.15 netmask 255.255.255.0` or `10.0.2.15/24`; the interface is called `vioif0` like
  NetBSD's virtio card, FreeBSD would say `vtnet0`; output as the BSDs print it: flags, `ether`, `inet ... netmask 0x... broadcast`),
  `route` (`show`, `add default A.B.C.D`, `delete default`), `dhclient [-t seconds] [interface]`. Changing anything needs root
  (`su`, password xyz; `euid == 0` is checked in the system calls `net_set` and `net_dhcp`, numbers 607 to 609); looking is free.
  Tests: the DHCP checks in `net_test.c` (a wrong address is put in first, DHCP must bring back 10.0.2.15, router 10.0.2.2, name
  server 10.0.2.3), the root commands in the shell test (job `shellnet`), and job `netconf` (command line with a fixed address, and `ip=dhcp`).
  Not done: renewing the lease, several interfaces (`ifconfig -a`), aliases, `up`/`down`, routes other than the default one, DNS.

### Tests of 2026-11-03 (state at that time)

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
- **Step 1, second part, done on 2026-11-04:** `dt.c` translates the addresses of devices through the `ranges` of the buses they sit on
  (`address ()`): the real firmware tree gives the UART as bus address 0x7e201000 and the GIC as 0x40041000, which the cores see at
  0xfe201000 and 0xff841000. `rpi4-qemu.dts` is laid out like the real `bcm2711.dtsi` (root with 2 address cells and 1 size cell, a
  `soc` bus with three `ranges`), so qemu's raspi4b boot tests it; it is a hand-written tree because qemu has no firmware that
  provides one. `make pcheck` has a job for this.
- **Step 4 (SD card), done on 2026-11-04, tested in qemu only:** `sdcard.c` is a polling driver for an SDHCI controller (reset, power,
  clock divider from the capabilities, CMD0/8/41/2/3/7/16, one sector per CMD17/CMD24 through the data port; SDSC and SDHC). The MBR
  of the card says where the disks are: the partitions of type 0x81 are `/dev/hda`, `/dev/hdb` and the stand-in for `/dev/fd1`, in
  that order; partition 1 (FAT) is left alone. `virtio.c` (the block layer) uses them when there is no virtio device, so
  everything above (buffer cache, Minix code, swap) is unchanged. `tools/make_sdcard.py` builds the card image (`make sd.img`, the
  three disk images as partitions, 128 MByte); `make run-rpi4` / `make run-rpi4-shell` boot the Pi images on qemu's raspi4b with it.
  Tests (`make pcheck`, 3 new jobs): all kernel tests (133 checks), the console test and the whole shell session pass on the
  emulated board. The interrupt-driven disk check of `proc_test.c` is skipped there (`blk_irq` is 0: the card is polled).
  - qemu puts its card on the controller at 0x7e300000, the real Pi 4 has the slot on EMMC2 (0x7e340000); `dt.c` uses whatever the
    tree says (`brcm,bcm2711-emmc2`).
  - Not done and not testable in qemu: the interrupt of the controller (a process could sleep instead of polling), DMA, multi-block
    commands (faster), the 4-bit bus, the 1.8 V switch (UHS), the clock and power of EMMC2 through the firmware mailbox (the firmware
    leaves the controller running when it starts the kernel, the driver resets it anyway), real cards that are slower than qemu's.
    The first boot on the board will show what the real controller needs on top.
- **Still open in step 1 (needs the board or the real firmware tree):**
  - Not verified against the real firmware tree: it may have more nodes and properties than the hand-written one (`dt.c` ignores
    what it does not know, but nothing replaces a test on the board; once the board arrives, save its tree with
    `dtc -I fs /proc/device_tree` from Raspberry Pi OS and test with that file).
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
