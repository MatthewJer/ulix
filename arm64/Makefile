# Ulix ARM64 port, stage 2: bare-metal skeleton for qemu-system-aarch64 -M virt
# Toolchain: brew install aarch64-elf-gcc aarch64-elf-binutils qemu
CROSS ?= aarch64-elf-
CC    = $(CROSS)gcc
LD    = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
CFLAGS = -O0 -g -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mgeneral-regs-only -std=c99 -Wall

# Disk image: the Minix image of the x86 version plus the AArch64 user programs.
# "-snapshot" keeps the file unmodified when running.
DISK ?= disk.img

OBJS = start.o vectors.o irq.o mmu.o virtio.o minixfs.o elf.o ulix-port.o glue.o arch_thread.o arch_console.o \
       bookfs_test.o thread_test.o proc_test.o kmain.o

all: ulix.elf ulix-console.elf $(DISK)

disk.img: ../bin-build/minixdata.img user/hello.elf user/cat.elf user/crash.elf user/forktest.elf user/sigtest.elf user/contest.elf tools/minix_add.py
	cp ../bin-build/minixdata.img $@
	# two deliberately broken variants of hello.elf for the loader tests:
	# e_machine = EM_386, and a code segment with flags RWX
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[18]=3; d[19]=0; open('bad-arch.elf','wb').write(d)"
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[68]=7; open('bad-wx.elf','wb').write(d)"
	python3 tools/minix_add.py $@ --grow=4096 hello64=user/hello.elf cat64=user/cat.elf crash64=user/crash.elf forktest64=user/forktest.elf sigtest64=user/sigtest.elf contest64=user/contest.elf \
		badarch64=bad-arch.elf badwx64=bad-wx.elf

# the user programs are rebuilt by their own Makefile whenever a source changed
user/%.elf: FORCE
	$(MAKE) -C user

FORCE:

ulix-console.elf: $(filter-out kmain.o,$(OBJS)) kmain-console.o ulix.ld
	$(LD) -T ulix.ld -o $@ $(filter-out kmain.o,$(OBJS)) kmain-console.o

kmain-console.o: kmain.c ulix.h
	$(CC) $(CFLAGS) -DCONSOLE_MODE -c -o $@ $<

ulix.elf: $(OBJS) ulix.ld
	$(LD) -T ulix.ld -o $@ $(OBJS)

# ---- kernel code taken from the book (ulix-book.nw) ----
# notangle assembles declarations / globals / functions from the book's chunks; port.py
# picks the functions listed in port.manifest.  The result is compiled unchanged.
GENDIR = gen
GENFILES = $(GENDIR)/ulix-decl.h $(GENDIR)/ulix-globals.c $(GENDIR)/ulix-functions.c $(GENDIR)/ulix-init.c

$(GENFILES): ../ulix-book.nw ulix-arm64.nw
	mkdir -p $(GENDIR)
	for r in ulix-decl.h ulix-globals.c ulix-functions.c ulix-init.c; do \
	  notangle -L -R$$r ../ulix-book.nw ulix-arm64.nw 2>/dev/null | sed 's/^#line .*//' > $(GENDIR)/$$r; \
	done

$(GENDIR)/ulix-port.c $(GENDIR)/ulix-api.h: $(GENFILES) port.manifest tools/port.py
	python3 tools/port.py $(GENDIR) port.manifest $(GENDIR)/ulix-port.c

BOOKFLAGS = -std=gnu99 -O0 -g -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -mgeneral-regs-only -Wall -Wno-unused -Wno-pointer-sign -I$(GENDIR)

ulix-port.o: $(GENDIR)/ulix-port.c
	$(CC) $(BOOKFLAGS) -c -o $@ $<

bookfs_test.o: bookfs_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=bookfs_unused_hash -c -o $@ $<

arch_thread.o: arch_thread.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=arch_unused_hash -c -o $@ $<

arch_console.o: arch_console.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=console_unused_hash -c -o $@ $<

thread_test.o: thread_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=thread_unused_hash -c -o $@ $<

proc_test.o: proc_test.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=proc_unused_hash -c -o $@ $<

# the book defines hash() inside its prototype chunk; rename it here so it is only linked once
glue.o: glue.c $(GENFILES) $(GENDIR)/ulix-api.h
	$(CC) $(BOOKFLAGS) -Dhash=glue_unused_hash -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: ulix.elf
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -nographic -kernel ulix.elf \
		-snapshot -drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0

# Run the kernel tests on a copy of the disk WITHOUT -snapshot, then check the file system of the
# modified copy on the host with an independent program (tools/minix_check.py).
verify: ulix.elf $(DISK)
	cp $(DISK) disk-after.img
	( qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -display none -serial file:verify.log -monitor none \
	    -kernel ulix.elf -drive file=disk-after.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 & \
	  P=$$!; sleep 45; kill $$P ) 2>/dev/null; true
	grep -E "FAIL|RESULT" verify.log; true
	python3 tools/minix_check.py disk-after.img --files | grep -E "keep|d1/|inodes|leaked|PROBLEM"

# Interactive console test: types into the UART of a second kernel image and checks the answers
console-test: ulix-console.elf $(DISK)
	python3 tools/console_test.py ulix-console.elf $(DISK)

clean:
	$(MAKE) -C user clean
	rm -rf gen
	rm -f *.o ulix.elf ulix-console.elf disk.img disk-after.img verify.log bad-arch.elf bad-wx.elf
