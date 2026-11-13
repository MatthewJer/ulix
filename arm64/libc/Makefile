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

PROGS = hello cat ls filetest readtest threadtest sockettest localetest

# CPython (third_party/cpython, issue #59): make PYTHON=1 also builds build/python.elf. It is configured out of the tree for
# --host=aarch64-linux-musl with the compiler wrapper musl-cc, which links with the script musl.ld like every program here
# (the build of CPython needs a CPython of the same version on the host).
PYTHON ?= 0
ifeq ($(PYTHON),1)
PROGS += python
endif
CPYTHON = $(abspath ../../third_party/cpython)
BUILD_PYTHON ?= $(shell command -v python3.12)

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

$(B)/python/Makefile: $(SYSROOT)/lib/libc.a musl-cc musl.ld python-setup.local
	mkdir -p $(B)/python/Modules
	cp python-setup.local $(B)/python/Modules/Setup.local          # (configure leaves an existing Setup.local alone)
	cd $(B)/python && MUSL_LDSCRIPT=$(abspath musl.ld) CC=$(abspath musl-cc) CPP="$(abspath musl-cc) -E" AR=$(CROSS)ar RANLIB=$(CROSS)ranlib \
	  READELF=$(CROSS)readelf $(CPYTHON)/configure --host=aarch64-linux-musl --build=$$($(CPYTHON)/config.guess) \
	  --with-build-python=$(BUILD_PYTHON) --disable-shared --without-ensurepip --disable-ipv6 \
	  ac_cv_file__dev_ptmx=no ac_cv_file__dev_ptc=no > configure.log 2>&1

$(B)/python.elf: $(B)/python/Makefile FORCE ../tools/mark_musl.py
	MUSL_LDSCRIPT=$(abspath musl.ld) $(MAKE) -C $(B)/python python.exe > $(B)/python/make.log 2>&1
	$(CROSS)strip -o $@ $(B)/python/python.exe
	python3 ../tools/mark_musl.py $@

FORCE:

clean:
	rm -rf $(B)
