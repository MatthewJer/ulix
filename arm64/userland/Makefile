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
# new programs of the AArch64 port (the network is not part of the original Ulix; the rule for them is further down)
NEWTOOLS = ping host ifconfig route dhclient shutdown poweroff reboot top
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
$(addprefix $(BUILD)/tools/,$(addsuffix .c,$(filter-out poweroff reboot,$(NEWTOOLS)))): $(BUILD)/tools/%.c: %.c $(BUILD)/ulixlib.h $(BUILD)/arm64-extra.h
	mkdir -p $(BUILD)/tools
	cp $< $@

# poweroff and reboot are the same program under other names
$(BUILD)/tools/poweroff.c $(BUILD)/tools/reboot.c: $(BUILD)/tools/%.c: shutdown.c
	mkdir -p $(BUILD)/tools
	cp $< $@

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
