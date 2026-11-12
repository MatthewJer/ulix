# User programs linked with musl (unmodified, third_party/musl). See chapter "The C Library: musl on Ulix" (lit/libc.nw).
#
#   make              builds musl once (libc/build/musl) and every program in PROGS as build/<name>.elf
#
# The programs speak the Linux system call interface (linux_abi.c in the kernel); mark_musl.py tells the kernel's loader.

CROSS ?= aarch64-elf-
CC     = $(CROSS)gcc
MUSL   = ../../third_party/musl
B      = build
SYSROOT = $(abspath $(B)/sysroot)

# the flags of the kernel's user space: no PIC, no outlined atomics (they need a run-time check of the CPU features)
CFLAGS = -O1 -g0 -fno-pic -mno-outline-atomics -std=gnu11 -Wall

PROGS = hello

all: $(addprefix $(B)/,$(addsuffix .elf,$(PROGS)))

# musl: configure out of tree (the submodule is not touched), build the static library, install headers, libc.a and crt files
$(SYSROOT)/lib/libc.a:
	mkdir -p $(B)/musl
	cd $(B)/musl && CC=$(CROSS)gcc AR=$(CROSS)ar RANLIB=$(CROSS)ranlib CFLAGS="-O2 -fno-pic -mno-outline-atomics" \
	  $(abspath $(MUSL))/configure --target=aarch64 --disable-shared --prefix=$(SYSROOT) > configure.log 2>&1
	$(MAKE) -C $(B)/musl install > $(B)/musl/make.log 2>&1

$(B)/%.o: %.c $(SYSROOT)/lib/libc.a
	mkdir -p $(B)
	$(CC) $(CFLAGS) -nostdinc -isystem $(SYSROOT)/include -isystem $(shell $(CC) -print-file-name=include) -c -o $@ $<

$(B)/%.elf: $(B)/%.o musl.ld ../tools/mark_musl.py
	$(CC) -static -nostdlib -nostartfiles -mno-outline-atomics -z max-page-size=4096 -z noexecstack -T musl.ld -o $@ \
	  $(SYSROOT)/lib/crt1.o $(SYSROOT)/lib/crti.o $< $(SYSROOT)/lib/libc.a -lgcc $(SYSROOT)/lib/crtn.o
	python3 ../tools/mark_musl.py $@

clean:
	rm -rf $(B)
