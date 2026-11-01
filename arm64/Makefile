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

all: ulix.elf $(DISK)

disk.img: ../bin-build/minixdata.img user/hello.elf user/cat.elf user/crash.elf tools/minix_add.py
	cp ../bin-build/minixdata.img $@
	# two deliberately broken variants of hello.elf for the loader tests:
	# e_machine = EM_386, and a code segment with flags RWX
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[18]=3; d[19]=0; open('bad-arch.elf','wb').write(d)"
	python3 -c "d=bytearray(open('user/hello.elf','rb').read()); d[68]=7; open('bad-wx.elf','wb').write(d)"
	python3 tools/minix_add.py $@ --grow=4096 hello64=user/hello.elf cat64=user/cat.elf crash64=user/crash.elf \
		badarch64=bad-arch.elf badwx64=bad-wx.elf

user/%.elf:
	$(MAKE) -C user

ulix.elf: start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o elf.o ulix-port.o glue.o bookfs_test.o kmain.o ulix.ld
	$(LD) -T ulix.ld -o $@ start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o elf.o ulix-port.o glue.o bookfs_test.o kmain.o

# ---- kernel code taken from the book (ulix-book.nw) ----
# notangle assembles declarations / globals / functions from the book's chunks; port.py
# picks the functions listed in port.manifest.  The result is compiled unchanged.
GENDIR = gen
GENFILES = $(GENDIR)/ulix-decl.h $(GENDIR)/ulix-globals.c $(GENDIR)/ulix-functions.c

$(GENFILES): ../ulix-book.nw ulix-arm64.nw
	mkdir -p $(GENDIR)
	for r in ulix-decl.h ulix-globals.c ulix-functions.c; do \
	  notangle -L -R$$r ../ulix-book.nw ulix-arm64.nw 2>/dev/null | sed 's/^#line .*//' > $(GENDIR)/$$r; \
	done

$(GENDIR)/ulix-port.c: $(GENFILES) port.manifest tools/port.py
	python3 tools/port.py $(GENDIR) port.manifest $@

BOOKFLAGS = -std=gnu99 -O0 -g -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -mgeneral-regs-only -Wall -Wno-unused -Wno-pointer-sign -I$(GENDIR)

ulix-port.o: $(GENDIR)/ulix-port.c
	$(CC) $(BOOKFLAGS) -c -o $@ $<

bookfs_test.o: bookfs_test.c $(GENFILES)
	$(CC) $(BOOKFLAGS) -Dhash=bookfs_unused_hash -c -o $@ $<

# the book defines hash() inside its prototype chunk; rename it here so it is only linked once
glue.o: glue.c $(GENFILES)
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

clean:
	$(MAKE) -C user clean
	rm -rf gen
	rm -f *.o ulix.elf disk.img disk-after.img verify.log bad-arch.elf bad-wx.elf
