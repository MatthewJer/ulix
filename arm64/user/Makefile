# AArch64 user programs for Ulix. Freestanding, no FP/SIMD (the kernel does not save those registers).
CROSS ?= aarch64-elf-
CC     = $(CROSS)gcc
CFLAGS = -mno-outline-atomics -O1 -g0 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic \
         -mgeneral-regs-only -std=gnu99 -Wall
LDFLAGS = -nostdlib -static -z max-page-size=4096 -z noexecstack -T user.ld

PROGS = hello.elf cat.elf crash.elf forktest.elf sigtest.elf contest.elf cowtest.elf thtest.elf badptr.elf swaptest.elf screentest.elf stacktest.elf memtest.elf pipetest.elf

all: $(PROGS)

%.elf: crt0.o %.o ulib.o user.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.o $*.o ulib.o

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c ulib.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf
