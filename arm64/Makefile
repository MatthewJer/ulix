# Ulix ARM64 port, stage 2: bare-metal skeleton for qemu-system-aarch64 -M virt
# Toolchain: brew install aarch64-elf-gcc aarch64-elf-binutils qemu
CROSS ?= aarch64-elf-
CC    = $(CROSS)gcc
LD    = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
CFLAGS = -O0 -g -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mgeneral-regs-only -std=c99 -Wall

# Disk image: the Minix image of the x86 version. "-snapshot" keeps the file unmodified.
DISK ?= ../bin-build/minixdata.img

all: ulix.elf

ulix.elf: start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o kmain.o ulix.ld
	$(LD) -T ulix.ld -o $@ start.o vectors.o irq.o mmu.o sched.o user.o virtio.o minixfs.o kmain.o

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: ulix.elf
	qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128 -nographic -kernel ulix.elf \
		-snapshot -drive file=$(DISK),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0

clean:
	rm -f *.o ulix.elf
