GENDIR = gen

# The sources of the port are written as literate programs in lit/*.nw (see lit/Makefile for the book).
# Tangle them first, before make looks at any dependency; only files whose content changed are touched.
TANGLED := $(shell sh tools/tangle.sh)
# Ulix ARM64 port, stage 2: bare-metal skeleton for qemu-system-aarch64 -M virt
# Toolchain: brew install aarch64-elf-gcc aarch64-elf-binutils qemu
CROSS ?= aarch64-elf-
CC    = $(CROSS)gcc
LD    = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
CFLAGS = -I$(GENDIR) -mno-outline-atomics -O0 -g -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mgeneral-regs-only -std=c99 -Wall

# Disk image: the Minix image of the x86 version plus the AArch64 user programs.
# "-snapshot" keeps the file unmodified when running.
DISK ?= disk.img

OBJS = start.o vectors.o irq.o mmu.o virtio.o sdcard.o virtionet.o genet.o netdev.o net.o ip6.o tcp.o dhcp.o dns.o netconf.o echo.o net_test.o net_syscalls.o netd.o rcconf.o telnet.o stats.o minixfs.o elf.o ulix-port.o glue.o arch_thread.o arch_console.o arch_clock.o arch_swap.o arch_vt.o dt.o gic.o smp.o \
       bookfs_test.o thread_test.o proc_test.o kmain.o

all: ulix.elf ulix-console.elf ulix-shell.elf ulix.bin ulix-console.bin ulix-shell.bin $(DISK) $(GENDIR)/disk_expected.h

# the programs of lib-build/tools and init, compiled for AArch64 (see userland/Makefile)
USERLAND_BIN = userland/build/bin

userland/build/bin/init.elf: FORCE
	$(MAKE) -C userland

disk.img: $(wildcard userland/build/bin/*.elf) ../bin-build/minixdata.img etc/rc.conf user/hello.elf user/cat.elf user/crash.elf user/forktest.elf user/sigtest.elf user/contest.elf user/cowtest.elf user/thtest.elf user/badptr.elf user/swaptest.elf user/screentest.elf user/stacktest.elf user/memtest.elf userland/build/bin/init.elf tools/minix_add.py
	cp ../bin-build/minixdata.img $@
	# two deliberately broken variants of hello.elf for the loader tests:
	# e_machine = EM_386, and a code segment with flags RWX
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[18]=3; d[19]=0; open('bad-arch.elf','wb').write(d)"
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[68]=7; open('bad-wx.elf','wb').write(d)"
	python3 tools/minix_add.py $@ --grow=4096 --chmod=/tmp:1777 hello64=user/hello.elf cat64=user/cat.elf crash64=user/crash.elf forktest64=user/forktest.elf sigtest64=user/sigtest.elf contest64=user/contest.elf cowtest64=user/cowtest.elf thtest64=user/thtest.elf badptr64=user/badptr.elf swaptest64=user/swaptest.elf screentest64=user/screentest.elf stacktest64=user/stacktest.elf memtest64=user/memtest.elf \
		badarch64=bad-arch.elf badwx64=bad-wx.elf \
		/init=$(USERLAND_BIN)/init.elf /etc/rc.conf=etc/rc.conf \
		$$(for f in $(USERLAND_BIN)/*.elf; do n=$$(basename $$f .elf); [ $$n != init ] && echo /bin/$$n=$$f; done)

# the user programs are rebuilt by their own Makefile whenever a source changed
# size and checksum of files on the disk, computed by the independent Python parser
# the second disk (/dev/hdb, mounted on /tmp like on the x86): an empty Minix file system of 32 MByte
DISK2 ?= disk2.img
$(DISK2): tools/minix_mkfs.py
	python3 tools/minix_mkfs.py $@ 32768

# the third disk (stands in for the floppy /dev/fd1, mounted on /mnt): 1440 KByte like a floppy, with a.txt for testcp
DISK3 ?= disk3.img
$(DISK3): tools/minix_mkfs.py tools/minix_add.py README.md
	python3 tools/minix_mkfs.py $@ 1440
	python3 tools/minix_add.py $@ /a.txt=README.md

$(GENDIR)/disk_expected.h: $(DISK) tools/disk_expected.py tools/minix_check.py
	mkdir -p $(GENDIR)
	python3 tools/disk_expected.py $(DISK) /init /bin/vi /hello.c /test.txt > $@

user/%.elf: FORCE
	$(MAKE) -C user

# the book of the ARM64 part (lit/build/ulix-arm64.pdf)
book:
	$(MAKE) -C lit

.PHONY: book

FORCE:

ulix-console.elf: $(filter-out kmain.o,$(OBJS)) kmain-console.o ulix.ld
	$(LD) -T ulix.ld -o $@ $(filter-out kmain.o,$(OBJS)) kmain-console.o

kmain.o kmain-console.o kmain-shell.o: $(GENDIR)/disk_expected.h

kmain-console.o: kmain.c ulix.h
	$(CC) $(CFLAGS) -DCONSOLE_MODE -c -o $@ $<

# ulix-shell.elf boots like the x86 kernel: it starts /init, which starts /bin/login and the shell
ulix-shell.elf: $(filter-out kmain.o,$(OBJS)) kmain-shell.o ulix.ld
	$(LD) -T ulix.ld -o $@ $(filter-out kmain.o,$(OBJS)) kmain-shell.o

kmain-shell.o: kmain.c ulix.h
	$(CC) $(CFLAGS) -DCONSOLE_MODE -DCONSOLE_PROGRAM='"/init"' -c -o $@ $<

# ulix-net.elf: the kernel for the network test (no disk, no processes, see net_test.c); "make net-test" runs it
kmain-net.o: kmain.c ulix.h
	$(CC) $(CFLAGS) -DNET_MODE -c -o $@ $<

ulix-net.elf: $(filter-out kmain.o,$(OBJS)) kmain-net.o ulix.ld
	$(LD) -T ulix.ld -o $@ $(filter-out kmain.o,$(OBJS)) kmain-net.o

# qemu boots flat images in the format of the Linux arm64 boot protocol (and then passes the device tree in x0), not ELF files
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

ulix.elf: $(OBJS) ulix.ld
	$(LD) -T ulix.ld -o $@ $(OBJS)

# ---- kernel code taken from the book (ulix-book.nw) ----
# notangle assembles declarations / globals / functions from the book's chunks; port.py
# picks the functions listed in port.manifest.  The result is compiled unchanged.
GENFILES = $(GENDIR)/ulix-decl.h $(GENDIR)/ulix-globals.c $(GENDIR)/ulix-functions.c $(GENDIR)/ulix-init.c

$(GENFILES): ../ulix-book.nw ulix-arm64.nw
	mkdir -p $(GENDIR)
	for r in ulix-decl.h ulix-globals.c ulix-functions.c ulix-init.c; do \
	  notangle -L -R$$r ../ulix-book.nw ulix-arm64.nw 2>/dev/null | sed 's/^#line .*//' > $(GENDIR)/$$r; \
	done

$(GENDIR)/ulix-port.c $(GENDIR)/ulix-api.h: $(GENFILES) port.manifest tools/port.py
	python3 tools/port.py $(GENDIR) port.manifest $(GENDIR)/ulix-port.c
	# several CPUs (smp.c): the book's scheduler () and u_kill () are wrapped; the wrappers have the book's names
	sed -i.bak -e 's/^void scheduler (context_t \*r, int source) {/void book_scheduler (context_t *r, int source) {/' \
	           -e 's/^int u_kill (int pid, int signo) {/int book_u_kill (int pid, int signo) {/' $(GENDIR)/ulix-port.c && rm $(GENDIR)/ulix-port.c.bak
	grep -q 'book_scheduler (context_t' $(GENDIR)/ulix-port.c && grep -q 'book_u_kill (int' $(GENDIR)/ulix-port.c

BOOKFLAGS = -mno-outline-atomics -std=gnu99 -O0 -g -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -mgeneral-regs-only -Wall -Wno-unused -Wno-pointer-sign -I$(GENDIR)

ulix-port.o: $(GENDIR)/ulix-port.c
	$(CC) $(BOOKFLAGS) -c -o $@ $<

bookfs_test.o: bookfs_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=bookfs_unused_hash -c -o $@ $<

arch_thread.o: arch_thread.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=arch_unused_hash -c -o $@ $<

stats.o: stats.c $(GENFILES) $(GENDIR)/ulix-api.h
netd.o: netd.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=netd_unused_hash -c -o $@ $<

net_syscalls.o: net_syscalls.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=netsys_unused_hash -c -o $@ $<

arch_clock.o: arch_clock.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=clock_unused_hash -c -o $@ $<

arch_console.o: arch_console.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=console_unused_hash -c -o $@ $<

thread_test.o: thread_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=thread_unused_hash -c -o $@ $<

proc_test.o: proc_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=proc_unused_hash -c -o $@ $<

# the book defines hash() inside its prototype chunk; rename it here so it is only linked once
glue.o: glue.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=glue_unused_hash -c -o $@ $<

irq.o mmu.o virtio.o sdcard.o dt.o gic.o smp.o kmain.o kmain-console.o kmain-shell.o kmain-net.o: plat.h gic.h smp.h ulix.h arch_pte.h
virtionet.o genet.o netdev.o net.o ip6.o tcp.o dhcp.o dns.o netconf.o echo.o net_test.o rcconf.o telnet.o: net.h plat.h ulix.h cache.h delay.h
sdcard.o: delay.h

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Raspberry Pi 4B (see PRINCIPLES.md) ----
# The same sources as for qemu; only the boot code (which GByte holds the console UART) and the load address differ: the
# firmware loads kernel8.img at 0x80000.  The kernel finds everything else in the device tree that the firmware hands over.
start-rpi4.o: start.S
	$(CC) $(CFLAGS) -DBOOT_DEV_GB=3 -c -o $@ $<

ulix-rpi4.elf: $(filter-out start.o,$(OBJS)) start-rpi4.o ulix.ld
	$(LD) -T ulix.ld --defsym=KERNEL_PA=0x80000 -o $@ $(filter-out start.o,$(OBJS)) start-rpi4.o

kernel8.img: ulix-rpi4.elf
	$(OBJCOPY) -O binary $< $@

# the same for the console test and for the system with login and shell (they differ from the test kernel in kmain only)
ulix-rpi4-console.elf: $(filter-out start.o kmain.o,$(OBJS)) start-rpi4.o kmain-console.o ulix.ld
	$(LD) -T ulix.ld --defsym=KERNEL_PA=0x80000 -o $@ $(filter-out start.o kmain.o,$(OBJS)) start-rpi4.o kmain-console.o

ulix-rpi4-shell.elf: $(filter-out start.o kmain.o,$(OBJS)) start-rpi4.o kmain-shell.o ulix.ld
	$(LD) -T ulix.ld --defsym=KERNEL_PA=0x80000 -o $@ $(filter-out start.o kmain.o,$(OBJS)) start-rpi4.o kmain-shell.o

kernel8-console.img: ulix-rpi4-console.elf
	$(OBJCOPY) -O binary $< $@

kernel8-shell.img: ulix-rpi4-shell.elf
	$(OBJCOPY) -O binary $< $@

# qemu's raspi4b machine gets no device tree from a firmware: this one describes what it has (rpi4-qemu.dts)
rpi4-qemu.dtb: rpi4-qemu.dts
	dtc -I dts -O dtb -o $@ $<

# the SD card: partition table, a boot partition (empty here) and the three disks as partitions of type 0x81
sd.img: tools/make_sdcard.py $(DISK) $(DISK2) $(DISK3)
	python3 tools/make_sdcard.py $@ $(DISK) $(DISK2) $(DISK3)

# The SD card for the real board: the same card image, but with the boot partition filled: the firmware files (not in the
# repository: put start4.elf, fixup4.dat and bcm2711-rpi-4-b.dtb from github.com/raspberrypi/firmware, directory boot/, into
# the directory firmware/), config.txt, cmdline.txt (maxcpus=1: the other cores wait in a spin table, see dt.c) and the kernel.
# Write it to a card with: sudo dd if=sd-pi.img of=/dev/rdiskN bs=4m   (N: see "diskutil list"; unmount the card first)
sd-pi.img: kernel8-shell.img rpi4-config.txt rpi4-cmdline.txt tools/make_sdcard.py $(DISK) $(DISK2) $(DISK3)
	rm -rf sd-boot && mkdir sd-boot
	if [ -d firmware ]; then cp -R firmware/. sd-boot/; else echo "note: no directory firmware/: the boot partition lacks the firmware files"; fi
	cp rpi4-config.txt sd-boot/config.txt
	cp rpi4-cmdline.txt sd-boot/cmdline.txt
	cp kernel8-shell.img sd-boot/kernel8.img
	python3 tools/make_sdcard.py --boot sd-boot $@ $(DISK) $(DISK2) $(DISK3)

# the system with login and shell on the emulated Pi (Ctrl-A X leaves qemu)
run-rpi4-shell: kernel8-shell.img rpi4-qemu.dtb sd.img
	qemu-system-aarch64 -M raspi4b -dtb rpi4-qemu.dtb -serial stdio -display none -kernel kernel8-shell.img -snapshot \
		-drive file=sd.img,if=sd,format=raw

run-rpi4: kernel8.img rpi4-qemu.dtb sd.img
	qemu-system-aarch64 -M raspi4b -dtb rpi4-qemu.dtb -serial stdio -display none -kernel kernel8.img -snapshot \
		-drive file=sd.img,if=sd,format=raw

run: ulix.bin
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -nographic -kernel ulix.bin \
		-snapshot -drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0

# Run the kernel tests on a copy of the disk WITHOUT -snapshot, then check the file system of the
# modified copy on the host with an independent program (tools/minix_check.py).
verify: ulix.bin $(DISK)
	cp $(DISK) disk-after.img
	( qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -display none -serial file:verify.log -monitor none \
	    -kernel ulix.bin -drive file=disk-after.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 & \
	  P=$$!; sleep 45; kill $$P ) 2>/dev/null; true
	grep -E "FAIL|RESULT" verify.log; true
	python3 tools/minix_check.py disk-after.img --files > verify-fs.log; grep -E "keep|d1/|inodes|leaked|PROBLEM" verify-fs.log; \
	  ! grep -q "PROBLEM:" verify-fs.log

# the same kernel tests on several machines; the kernel finds everything in the device tree that qemu passes
MACHINES = "virt,gic-version=2|-smp 1" "virt,gic-version=3|-smp 1" "virt,gic-version=2|-smp 2" "virt,gic-version=3|-smp 2" \
           "virt,gic-version=2|-smp 4" "virt,gic-version=3,virtualization=on|-smp 4" "virt,gic-version=3,virtualization=on|-smp 2"
machines: ulix.bin ulix-console.bin ulix-shell.bin $(DISK)
	@for cfg in $(MACHINES); do M=$${cfg%%|*}; X=$${cfg##*|}; printf "  %-48s " "$$M $$X"; \
	  tools/run_kernel_tests.sh ulix.bin $(DISK) machine.log -M $$M $$X | tr '\n' ' ' | sed 's/RESULT: //'; echo; \
	  grep -q "^RESULT: all checks passed" machine.log || exit 1; done
	@for mem in 64 512 1024 3072; do printf "  %-48s " "RAM $$mem MByte, 2 CPUs"; QEMU_MEM=$$mem \
	  tools/run_kernel_tests.sh ulix.bin $(DISK) machine.log -smp 2 | tr '\n' ' ' | sed 's/RESULT: //'; echo; \
	  grep -q "^RESULT: all checks passed" machine.log || exit 1; done
	@echo "  console test and shell test with 2 CPUs and GICv3"
	QEMU_M=virt,gic-version=3 QEMU_EXTRA="-smp 2" python3 tools/console_test.py ulix-console.bin $(DISK) | tail -1
	QEMU_M=virt,gic-version=3 QEMU_EXTRA="-smp 2" python3 tools/shell_test.py ulix-shell.bin $(DISK) | tail -1

# Use the system: boots /init -> login -> shell on this terminal. Ctrl-C ends the foreground program, Ctrl-T 2 switches
# to terminal 2, Ctrl-] opens the kernel shell, Ctrl-A X leaves qemu. Changes to the disk are discarded (-snapshot).
run-shell: ulix-shell.bin $(DISK)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -nographic -kernel ulix-shell.bin -snapshot \
		-drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 \
		-netdev user,id=n0 -device virtio-net-device,netdev=n0

# Everything: kernel test suites (EL1 with the legacy and EL2 with the modern virtio transport), the file system
# check of the modified image, the console test and the complete shell session.
check: ulix.bin ulix-console.bin ulix-shell.bin $(DISK)
	@echo "== kernel tests, started at EL1, virtio legacy transport"
	tools/run_kernel_tests.sh ulix.bin $(DISK) check-el1.log
	@echo "== kernel tests, started at EL2, virtio modern transport"
	tools/run_kernel_tests.sh ulix.bin $(DISK) check-el2.log -M virt,virtualization=on -global virtio-mmio.force-legacy=false
	@echo "== kernel tests on other machines: GICv2 and GICv3, 1, 2 and 4 CPUs, EL1 and EL2, 64 MByte to 2 GByte of RAM"
	$(MAKE) --no-print-directory machines
	@echo "== write tests on a copy of the disk, then independent check of the file system"
	$(MAKE) --no-print-directory verify
	@echo "== console test"
	$(MAKE) --no-print-directory console-test
	@echo "== shell session"
	$(MAKE) --no-print-directory shell-test
	@echo "== ALL TESTS PASSED"

# The same tests as "check", all at once: one qemu per test, JOBS of them in parallel (make pcheck JOBS=8), logs in check-logs/
JOBS ?= 6
pcheck: ulix.bin ulix-console.bin ulix-shell.bin ulix-net.bin kernel8.img kernel8-console.img kernel8-shell.img rpi4-qemu.dtb sd.img $(DISK) $(DISK2) $(DISK3)
	DISK=$(DISK) DISK2=$(DISK2) DISK3=$(DISK3) tools/parallel_check.sh $(JOBS)

# The serial disk: the kernel tests with a second serial port, which tools/serial_disk.py serves from a copy of disk3.img
sdisk-test: ulix.bin $(DISK) $(DISK3)
	python3 tools/sdisk_test.py ulix.bin $(DISK) $(DISK3)

# the network thread (netd): the console kernel with "netecho" answers on port 7 while it runs its program
netd-test: ulix-console.bin $(DISK)
	python3 tools/netd_test.py ulix-console.bin $(DISK)

# a cable that is plugged in after the boot: the network thread asks DHCP by itself
dhcp-retry-test: ulix-console.bin $(DISK)
	python3 tools/dhcp_retry_test.py ulix-console.bin $(DISK)

# the telnet server: a client of the host logs in on terminal 5, runs programs, hangs up; the server can be switched off in /etc/rc.conf
telnet-test: ulix-shell.bin $(DISK)
	python3 tools/telnet_test.py ulix-shell.bin $(DISK)

# The network: the net test kernel talks to a Python program on the host through qemu's user-mode network
net-test: ulix-net.bin
	python3 tools/net_test.py ulix-net.bin

# Interactive console test: types into the UART of a second kernel image and checks the answers
console-test: ulix-console.bin $(DISK)
	python3 tools/console_test.py ulix-console.bin $(DISK)

# A complete session: init, login, shell and the programs of lib-build/tools
shell-test: ulix-shell.bin $(DISK)
	python3 tools/shell_test.py ulix-shell.bin $(DISK)

clean:
	$(MAKE) -C user clean
	rm -rf gen
	$(MAKE) -C userland clean
	rm -rf check-logs
	rm -rf sd-boot; rm -f sd.img sd-pi.img kernel8*.img ulix-rpi4*.elf start-rpi4.o rpi4-qemu.dtb
	rm -f disk2.img disk3.img verify-fs.log check-el1.log check-el2.log *.o ulix.elf ulix-console.elf ulix-shell.elf disk.img disk-after.img verify.log bad-arch.elf bad-wx.elf
