GENDIR = gen
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

OBJS = start.o vectors.o irq.o mmu.o virtio.o minixfs.o elf.o ulix-port.o glue.o arch_thread.o arch_console.o arch_clock.o arch_swap.o arch_vt.o dt.o gic.o smp.o \
       bookfs_test.o thread_test.o proc_test.o kmain.o

all: ulix.elf ulix-console.elf ulix-shell.elf ulix.bin ulix-console.bin ulix-shell.bin $(DISK) $(GENDIR)/disk_expected.h

# the programs of lib-build/tools and init, compiled for AArch64 (see userland/Makefile)
USERLAND_BIN = userland/build/bin

userland/build/bin/init.elf: FORCE
	$(MAKE) -C userland

disk.img: $(wildcard userland/build/bin/*.elf) ../bin-build/minixdata.img user/hello.elf user/cat.elf user/crash.elf user/forktest.elf user/sigtest.elf user/contest.elf user/cowtest.elf user/thtest.elf user/badptr.elf user/swaptest.elf user/screentest.elf user/stacktest.elf user/memtest.elf userland/build/bin/init.elf tools/minix_add.py
	cp ../bin-build/minixdata.img $@
	# two deliberately broken variants of hello.elf for the loader tests:
	# e_machine = EM_386, and a code segment with flags RWX
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[18]=3; d[19]=0; open('bad-arch.elf','wb').write(d)"
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[68]=7; open('bad-wx.elf','wb').write(d)"
	python3 tools/minix_add.py $@ --grow=4096 --chmod=/tmp:1777 hello64=user/hello.elf cat64=user/cat.elf crash64=user/crash.elf forktest64=user/forktest.elf sigtest64=user/sigtest.elf contest64=user/contest.elf cowtest64=user/cowtest.elf thtest64=user/thtest.elf badptr64=user/badptr.elf swaptest64=user/swaptest.elf screentest64=user/screentest.elf stacktest64=user/stacktest.elf memtest64=user/memtest.elf \
		badarch64=bad-arch.elf badwx64=bad-wx.elf \
		/init=$(USERLAND_BIN)/init.elf \
		$$(for f in $(USERLAND_BIN)/*.elf; do n=$$(basename $$f .elf); [ $$n != init ] && echo /bin/$$n=$$f; done)

# the user programs are rebuilt by their own Makefile whenever a source changed
# size and checksum of files on the disk, computed by the independent Python parser
$(GENDIR)/disk_expected.h: $(DISK) tools/disk_expected.py tools/minix_check.py
	mkdir -p $(GENDIR)
	python3 tools/disk_expected.py $(DISK) /init /bin/vi /hello.c /test.txt > $@

user/%.elf: FORCE
	$(MAKE) -C user

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

irq.o mmu.o virtio.o dt.o gic.o smp.o kmain.o kmain-console.o kmain-shell.o: plat.h gic.h smp.h ulix.h arch_pte.h

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

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
		-drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0

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
pcheck: ulix.bin ulix-console.bin ulix-shell.bin $(DISK)
	DISK=$(DISK) tools/parallel_check.sh $(JOBS)

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
	rm -f verify-fs.log check-el1.log check-el2.log *.o ulix.elf ulix-console.elf ulix-shell.elf disk.img disk-after.img verify.log bad-arch.elf bad-wx.elf
