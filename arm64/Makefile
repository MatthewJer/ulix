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
	python3 tools/minix_add.py $@ hello64=user/hello.elf cat64=user/cat.elf crash64=user/crash.elf \
		badarch64=bad-arch.elf badwx64=bad-wx.elf

user/%.elf:
	$(MAKE) -C user

ulix.elf: start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o elf.o kmain.o ulix.ld
	$(LD) -T ulix.ld -o $@ start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o elf.o kmain.o

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: ulix.elf
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -nographic -kernel ulix.elf \
		-snapshot -drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0

clean:
	$(MAKE) -C user clean
	rm -f *.o ulix.elf disk.img bad-arch.elf bad-wx.elf
