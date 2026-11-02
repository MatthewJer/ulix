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

# recurse.c contains x86 inline assembler (it demonstrates stack growth of an x86 process).
# swapper.c needs the x86 swap system calls.
SKIP     = recurse swapper
TOOLS    = $(filter-out $(SKIP),$(patsubst $(TOOLSRC)/%.c,%,$(wildcard $(TOOLSRC)/*.c)))

# GCC 4.4 accepted a lot that current compilers reject (return without value, missing types);
# these stay warnings. -fgnu89-inline: "inline" functions of the library also get an out-of-line copy.
# -O0 like the x86 build (lib-build/Makefile): programs such as thread.c wait in loops on plain global variables
CFLAGS = -O0 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic -mgeneral-regs-only \
         -fgnu89-inline -std=gnu99 -w \
         -Wno-error=return-mismatch -Wno-error=implicit-int -Wno-error=implicit-function-declaration \
         -Wno-error=int-conversion -Wno-error=incompatible-pointer-types
LDFLAGS = -static -z max-page-size=4096 -T ../user/user.ld

PROGS = $(addprefix $(BUILD)/bin/,$(addsuffix .elf,$(TOOLS) init swapper))

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

$(BUILD)/tools/%.o: $(BUILD)/tools/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# swapper: stub, see swapper-stub.c
$(BUILD)/tools/swapper.c: swapper-stub.c $(BUILD)/ulixlib.h
	mkdir -p $(BUILD)/tools
	cp $< $@

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
