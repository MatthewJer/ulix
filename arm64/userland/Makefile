# The programs here are written in lit/*.nw and tangled (userland/musl/ulixcompat.c, ...). The top-level Makefile tangles first; this line does the same
# for "make -C userland" alone, otherwise a stale tangled file is compiled (cpif keeps the dates of files that did not change).
TANGLED := $(shell sh ../tools/tangle.sh)
# The user space of Ulix for AArch64: the library (ulixlib, from ../../ulix-book.nw) and the programs of
# ../../lib-build/tools (plain C files) and init (from the book), all compiled unchanged.
#
#   make              builds build/bin/<name>.elf for every program that compiles
#   make -n list      shows the programs
#
# The programs include "../ulixlib.h", so they are copied to build/tools/ first (a copy next to a
# generated header; otherwise the stale x86 header in ../../lib-build/ would be found first).

CROSS ?= aarch64-elf-
CC     = $(CROSS)gcc
LD     = $(CROSS)ld

BOOK     = ../../ulix-book.nw
TOOLSRC  = ../../lib-build/tools
BUILD    = build

SKIP     =
# swapper.c starts to page out when only 500 frames (2 MByte) are in use ("init_frames - 500"): right for the 64 MByte PC, but on a machine with
# 128 MByte to 4 GByte it swapped from the first program on. Here the threshold is a fraction of the RAM: 1/16 of the frames must stay free
$(BUILD)/tools/swapper.c: $(TOOLSRC)/swapper.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h
	mkdir -p $(BUILD)/tools
	sed -e 's|#include "../ulixlib.h"|&\n#include "../arm64-extra.h"|' -e 's|init_frames - 500|(total_frames () / 16)|g' $< > $@
	grep -q 'total_frames () / 16' $@

# new programs of the AArch64 port (the network is not part of the original Ulix; the rule for them is further down)
NEWTOOLS = ping ping6 host ifconfig route dhclient shutdown poweroff reboot top sntp rand https daemon service heartbeat echod ptytest telnetd sshd
TOOLS    = $(filter-out $(SKIP),$(patsubst $(TOOLSRC)/%.c,%,$(wildcard $(TOOLSRC)/*.c))) $(NEWTOOLS)

# GCC 4.4 accepted a lot that current compilers reject (return without value, missing types);
# these stay warnings. -fgnu89-inline: "inline" functions of the library also get an out-of-line copy.
# -O0 like the x86 build (lib-build/Makefile): programs such as thread.c wait in loops on plain global variables
# -fsanitize=integer-divide-by-zero with trap: an integer division by zero ends the process, as the fault of the x86 does
# (AArch64 itself returns 0); -mno-outline-atomics: no helper functions of libgcc
CFLAGS = -mno-outline-atomics -fsanitize=integer-divide-by-zero -fsanitize-undefined-trap-on-error -O0 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic -mgeneral-regs-only \
         -fgnu89-inline -std=gnu99 -w \
         -Wno-error=return-mismatch -Wno-error=implicit-int -Wno-error=implicit-function-declaration \
         -Wno-error=int-conversion -Wno-error=incompatible-pointer-types
LDFLAGS = -static -z max-page-size=4096 -T ../user/user.ld

PROGS = $(addprefix $(BUILD)/bin/,$(addsuffix .elf,$(TOOLS) init))

all: $(PROGS)

$(BUILD)/ulixlib.h $(BUILD)/ulixlib.c: $(BOOK)
	mkdir -p $(BUILD)
	notangle -L -Rulixlib.h $(BOOK) 2>/dev/null | sed 's/^\#line .*//' > $(BUILD)/ulixlib.h
	notangle -L -Rulixlib.c $(BOOK) 2>/dev/null | sed 's/^\#line .*//' > $(BUILD)/ulixlib.c

$(BUILD)/ulixlib.o: $(BUILD)/ulixlib.c $(BUILD)/ulixlib.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/crt0.o: ../user/crt0.S
	mkdir -p $(BUILD)
	$(CC) -c -o $@ $<

$(BUILD)/tools/%.c: $(TOOLSRC)/%.c $(BUILD)/ulixlib.h
	mkdir -p $(BUILD)/tools
	cp $< $@

# recurse.c reads the stack pointer with x86 inline assembler ("mov %esp"); the only change for AArch64 is the register name
$(BUILD)/tools/recurse.c: $(TOOLSRC)/recurse.c $(BUILD)/ulixlib.h
	mkdir -p $(BUILD)/tools
	sed 's/asm volatile ("mov %%esp, %0" : "=r"(x))/asm volatile ("mov %x0, sp" : "=r"(x))/' $< > $@
	grep -q 'mov %x0, sp' $@

# free.c divides by the constant MEM_SIZE (the 64 MByte of the PC); here the size of the RAM is asked for instead
$(BUILD)/arm64-extra.h: arm64-extra.h
	mkdir -p $(BUILD)
	cp $< $@

$(BUILD)/tools/free.c: $(TOOLSRC)/free.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h
	mkdir -p $(BUILD)/tools
	sed -e 's|#include "../ulixlib.h"|&\n#include "../arm64-extra.h"|' -e 's|MEM_SIZE|(total_frames () * 4096)|g' $< > $@
	grep -q 'total_frames' $@

# new programs of the AArch64 port (the network is not part of the original Ulix): ping uses raw ICMP sockets, ifconfig, route and dhclient
# are modelled on the BSD programs of the same names
$(addprefix $(BUILD)/tools/,$(addsuffix .c,$(filter-out poweroff reboot https sshd,$(NEWTOOLS)))): $(BUILD)/tools/%.c: %.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h
	mkdir -p $(BUILD)/tools
	cp $< $@

# poweroff and reboot are the same program under other names
$(BUILD)/tools/poweroff.c $(BUILD)/tools/reboot.c: $(BUILD)/tools/%.c: shutdown.c
	mkdir -p $(BUILD)/tools
	cp $< $@

# ---- TLS (chapter "Security: random numbers, the time of day and TLS") ----
# make TLS=0 leaves BearSSL and everything that speaks TLS out: https is then a small program that says so (https-off.c).
# BearSSL (third_party/bearssl, unmodified) is compiled at -O2 into an archive; the linker takes what https uses.
TLS ?= 1
BEARSSL  = ../../third_party/bearssl
# a file that records the mode of the last build: when TLS changes, it is made new and everything that depends on it is rebuilt
# (made while make reads this file, because make does not rebuild a missing file that only sits in the middle of a chain)
TLS_STAMP := $(shell test -f $(BUILD)/tls-$(TLS).stamp || { mkdir -p $(BUILD); rm -f $(BUILD)/tls-*.stamp; touch $(BUILD)/tls-$(TLS).stamp; })

ifeq ($(TLS),1)
BEARSSL_SRCS = $(shell find $(BEARSSL)/src -name '*.c' 2>/dev/null)
BEARSSL_OBJS = $(patsubst $(BEARSSL)/src/%.c,$(BUILD)/bearssl/%.o,$(BEARSSL_SRCS))
BRFLAGS = -mno-outline-atomics -O2 -ffreestanding -nostdlib -fno-stack-protector -fno-pic -mgeneral-regs-only -std=gnu99 -w \
          -I$(BEARSSL)/inc -I$(BEARSSL)/src -Itls -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0

$(BUILD)/bearssl/%.o: $(BEARSSL)/src/%.c tls/string.h
	@mkdir -p $(dir $@)
	$(CC) $(BRFLAGS) -c -o $@ $<

$(BUILD)/libbearssl.a: $(BEARSSL_OBJS)
	@test -n "$(BEARSSL_OBJS)" || { echo "third_party/bearssl is empty (the submodule is not checked out)"; exit 1; }
	rm -f $@
	$(CROSS)ar rcs $@ $^

$(BUILD)/tools/https.c: https.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h $(BUILD)/tls-1.stamp
	mkdir -p $(BUILD)/tools
	cp $< $@

$(BUILD)/tools/https.o: $(BUILD)/tools/https.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tls-glue.o: tls/tlsglue.c
	$(CC) $(BRFLAGS) -c -o $@ $<

$(BUILD)/tls-libc.o: tls/libc.c
	$(CC) $(BRFLAGS) -c -o $@ $<

$(BUILD)/bin/https.elf: $(BUILD)/crt0.o $(BUILD)/tools/https.o $(BUILD)/tls-glue.o $(BUILD)/tls-libc.o $(BUILD)/ulixlib.o $(BUILD)/libbearssl.a
	mkdir -p $(BUILD)/bin
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/crt0.o $(BUILD)/tools/https.o $(BUILD)/tls-glue.o $(BUILD)/tls-libc.o $(BUILD)/ulixlib.o $(BUILD)/libbearssl.a

# the SSH server (chapter "An SSH server"): the protocol is an ordinary program (CFLAGS), the cryptography file sees BearSSL's header files
# and is compiled like the library (BRFLAGS); both are linked with the same libc functions and the same archive as https
$(BUILD)/tools/sshd.c: sshd.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h $(BUILD)/tls-1.stamp
	mkdir -p $(BUILD)/tools
	cp $< $@

$(BUILD)/tools/sshd.o: $(BUILD)/tools/sshd.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/ssh-crypto.o: sshcrypto.c tls/string.h
	$(CC) $(BRFLAGS) -c -o $@ $<

$(BUILD)/bin/sshd.elf: $(BUILD)/crt0.o $(BUILD)/tools/sshd.o $(BUILD)/ssh-crypto.o $(BUILD)/tls-libc.o $(BUILD)/ulixlib.o $(BUILD)/libbearssl.a
	mkdir -p $(BUILD)/bin
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/crt0.o $(BUILD)/tools/sshd.o $(BUILD)/ssh-crypto.o $(BUILD)/tls-libc.o $(BUILD)/ulixlib.o $(BUILD)/libbearssl.a
else
$(BUILD)/tools/https.c: https-off.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h $(BUILD)/tls-0.stamp
	mkdir -p $(BUILD)/tools
	cp $< $@

$(BUILD)/tools/sshd.c: sshd-off.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h $(BUILD)/tls-0.stamp
	mkdir -p $(BUILD)/tools
	cp $< $@
endif

# readelf.c reads ELF32 headers; the programs here are ELF64 files
$(BUILD)/tools/readelf.c: readelf64.c $(BUILD)/ulixlib.h
	mkdir -p $(BUILD)/tools
	cp $< $@

$(BUILD)/tools/%.o: $(BUILD)/tools/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# init: the book defines umain(); crt0 calls main()
$(BUILD)/init.c: $(BOOK) $(BUILD)/ulixlib.h
	notangle -L -R'lib-build/init.c' $(BOOK) 2>/dev/null | sed 's/^\#line .*//' > $@

$(BUILD)/init.o: $(BUILD)/init.c
	$(CC) $(CFLAGS) -Dumain=main -c -o $@ $<

$(BUILD)/bin/init.elf: $(BUILD)/crt0.o $(BUILD)/init.o $(BUILD)/ulixlib.o
	mkdir -p $(BUILD)/bin
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/bin/%.elf: $(BUILD)/crt0.o $(BUILD)/tools/%.o $(BUILD)/ulixlib.o
	mkdir -p $(BUILD)/bin
	$(LD) $(LDFLAGS) -o $@ $^

list:
	@echo $(TOOLS) init

clean:
	rm -rf $(BUILD)

.SECONDARY:

# ---- programs linked with musl instead of ulixlib (issue #35, section 51 of ../ARM64-PORT.md) ----
# The program text is the book's. Its #include "../ulixlib.h" finds musl/ulixlib.h (copied to build/musl), which includes musl's headers and adds what
# the book has more (see "The programs of the book on musl" in lit/userland.nw). Every name in MUSLTOOLS is linked with musl; the others keep ulixlib.
MUSLTOOLS = echo cat wc grep diff mkdir rmdir rm touch stat cp ls hexdump bindump ascii print args exit42 trunc chmod chown chgrp df sync kill fault-div0 fault-mem testcp stdio inode keys clear free ps pw swapper top ping ping6 host shutdown poweroff reboot sntp rand daemon service heartbeat echod fork1 fork2 fork3 fbomb filecomm ptree vi tp su sh init2 telnetd login xlogin init readelf memwrite recurse ifconfig route dhclient ptytest
SYSROOT   = $(abspath ../libc/build/sysroot)
PORT_PFD ?= 64
# (a change of PORT_PFD must rebuild the programs: the stamp is a prerequisite of their objects)
$(BUILD)/pfd-$(PORT_PFD).stamp:
	mkdir -p $(BUILD)
	rm -f $(BUILD)/pfd-*.stamp
	touch $@

MCFLAGS   = -DMAX_PFD=$(PORT_PFD) -O0 -fno-pic -mno-outline-atomics -fsanitize=integer-divide-by-zero -fsanitize-undefined-trap-on-error -std=gnu99 -w -Wno-error=return-mismatch -Wno-error=implicit-int -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
            -nostdinc -isystem $(SYSROOT)/include -isystem $(shell $(CC) -print-file-name=include)
MUSLPROGS = $(addprefix $(BUILD)/bin/,$(addsuffix .elf,$(MUSLTOOLS)))
# (the book's programs define O_CREAT = 0x200, O_APPEND, S_IF... themselves; musl's open would read 0x200 as Linux's O_TRUNC: those lines are dropped)
# programs that define a library function themselves (a teaching version of strchr, memcmp, ...): their copy gets another name
RENAME_wc   = strchr
RENAME_grep = strchr memmove
RENAME_diff = memcmp

$(SYSROOT)/lib/libc.a:
	$(MAKE) -C ../libc $@

$(BUILD)/musl/ulixlib.h: musl/ulixlib.h
	mkdir -p $(BUILD)/musl
	cp $< $@

$(BUILD)/musl/arm64-extra.h: arm64-extra.h
	mkdir -p $(BUILD)/musl
	cp $< $@

$(BUILD)/musl/tools/%.c: $(BUILD)/tools/%.c $(BUILD)/musl/arm64-extra.h
	mkdir -p $(BUILD)/musl/tools
	perl -pe '$$_ = "" if /^#define (O_\w+|S_\w+|SEEK_\w+)\s/; $(foreach f,$(RENAME_$*),s/\b$(f)\b/$*_$(f)/g;)' $< > $@

$(BUILD)/musl/tools/%.o: $(BUILD)/musl/tools/%.c $(BUILD)/musl/ulixlib.h $(SYSROOT)/lib/libc.a $(BUILD)/pfd-$(PORT_PFD).stamp
	$(CC) $(MCFLAGS) -c -o $@ $<

$(BUILD)/musl/ulixcompat.o: musl/ulixcompat.c musl/ulixlib.h $(SYSROOT)/lib/libc.a $(BUILD)/pfd-$(PORT_PFD).stamp
	mkdir -p $(BUILD)/musl
	$(CC) $(MCFLAGS) -Imusl -c -o $@ $<

$(MUSLPROGS): $(BUILD)/bin/%.elf: $(BUILD)/musl/tools/%.o $(BUILD)/musl/ulixcompat.o ../libc/musl.ld ../tools/mark_musl.py
	mkdir -p $(BUILD)/bin
	$(CC) -static -nostdlib -nostartfiles -mno-outline-atomics -z max-page-size=4096 -z noexecstack -T ../libc/musl.ld -o $@ \
	  $(SYSROOT)/lib/crt1.o $(SYSROOT)/lib/crti.o $(BUILD)/musl/tools/$*.o $(BUILD)/musl/ulixcompat.o $(SYSROOT)/lib/libc.a -lgcc $(SYSROOT)/lib/crtn.o
	python3 ../tools/mark_musl.py $@

# init: the book defines umain(); the same program text, linked with musl (crt1 calls main)
$(BUILD)/musl/tools/init.c: $(BUILD)/init.c
	mkdir -p $(BUILD)/musl/tools
	( echo '#define umain main'; sed 's|#include "ulixlib.h"|#include "../ulixlib.h"|' $< ) > $@
