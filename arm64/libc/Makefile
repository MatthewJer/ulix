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
ARCHIVE ?= 1
ifeq ($(ARCHIVE),1)
PROGS += bsdtar bsdunzip
endif

# CPython (third_party/cpython, issue #59): make PYTHON=1 also builds build/python.elf. It is configured out of the tree for
# --host=aarch64-linux-musl with the compiler wrapper musl-cc, which links with the script musl.ld like every program here
# (the build of CPython needs a CPython of the same version on the host).
PYTHON ?= 0
ifeq ($(PYTHON),1)
PROGS += python
endif
THIRD = $(abspath ../../third_party)
CPYTHON = $(THIRD)/cpython
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

# Foreign libraries (unmodified sources in third_party/, built out of tree, installed in the sysroot so that every program can link them).
# zlib is used by the Python module of the same name and by libarchive (the tar/zip program).
$(SYSROOT)/lib/libz.a: $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/zlib
	cd $(B)/zlib && CHOST=aarch64-linux-musl CC=$(abspath musl-cc) AR=$(CROSS)ar RANLIB=$(CROSS)ranlib CFLAGS="-O2" \
	  $(THIRD)/zlib/configure --static --prefix=$(SYSROOT) > configure.log 2>&1
	$(MAKE) -C $(B)/zlib install > $(B)/zlib/make.log 2>&1

# SQLite: the amalgamation (one C file) is made on the host from the sources (needs tclsh), and compiled for Ulix. No WAL and no memory-mapped
# i/o (they need shared memory and file mappings), no loadable extensions (no dlopen), temporary files in memory.
SQLITE_FLAGS = -Os -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_TEMP_STORE=2 \
               -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_DEFAULT_MEMSTATUS=0 -DHAVE_USLEEP=1 -DSQLITE_OMIT_DEPRECATED
$(SYSROOT)/lib/libsqlite3.a: $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/sqlite
	cd $(B)/sqlite && $(THIRD)/sqlite/configure > configure.log 2>&1 && $(MAKE) sqlite3.c > make.log 2>&1
	./musl-cc -c $(SQLITE_FLAGS) -o $(B)/sqlite/sqlite3.o $(B)/sqlite/sqlite3.c
	$(CROSS)ar rcs $@ $(B)/sqlite/sqlite3.o
	cp $(B)/sqlite/sqlite3.h $(B)/sqlite/sqlite3ext.h $(SYSROOT)/include/

# libffi (for the Python module ctypes): the upstream sources need autoconf, so the few files for AArch64 are compiled here, with the
# hand-written libc/fficonfig.h (what configure would have found) and ffi.h made from include/ffi.h.in. Calls into C work; callbacks need
# memory that is writable and executable at once, which the kernel's W^X rule refuses: closures.c is replaced by ffi_closures.c (write, then switch the
# page to read+execute).
FFI_SRC = src/prep_cif.c src/types.c src/raw_api.c src/java_raw_api.c src/aarch64/ffi.c src/aarch64/sysv.S
$(SYSROOT)/lib/libffi.a: $(SYSROOT)/lib/libc.a fficonfig.h ffi_closures.c
	mkdir -p $(B)/libffi
	$(RM) $(B)/libffi/*.o $@
	sed -e 's/@VERSION@/3.4.6/' -e 's/@TARGET@/AARCH64/' -e 's/@HAVE_LONG_DOUBLE@/1/' -e 's/@FFI_EXEC_TRAMPOLINE_TABLE@/0/' $(THIRD)/libffi/include/ffi.h.in > $(B)/libffi/ffi.h
	cp $(THIRD)/libffi/src/aarch64/ffitarget.h $(B)/libffi/
	for f in $(FFI_SRC); do ./musl-cc -c -O2 -DHAVE_CONFIG_H -I. -I$(B)/libffi -I$(THIRD)/libffi/include -I$(THIRD)/libffi/src/aarch64 -I$(THIRD)/libffi/src \
	  -o $(B)/libffi/$$(basename $$f).o $(THIRD)/libffi/$$f || exit 1; done
	./musl-cc -c -O2 -I$(B)/libffi -o $(B)/libffi/ffi_closures.o ffi_closures.c
	$(CROSS)ar rcs $@ $(B)/libffi/*.o
	cp $(B)/libffi/ffi.h $(B)/libffi/ffitarget.h $(SYSROOT)/include/

# dlopen and dlsym for a static program (dlshim.c): ctypes finds the C library and the interpreter itself in a table of the symbols of the
# executable. The table is made from the finished program, so the program is linked twice: first with an empty table, then with the real one.
$(B)/dl/dlshim.o: dlshim.c $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/dl
	./musl-cc -c -O1 -o $@ dlshim.c

# named semaphores for multiprocessing (semshim.c: the kernel keeps them, there is no shared memory); the linker sends the calls on semaphores to it
# (and ffi_prep_closure_loc to ffi_closures.c, which makes the closure page executable after libffi has written it)
$(B)/dl/semshim.o: semshim.c $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/dl
	./musl-cc -c -O1 -o $@ semshim.c
SEMWRAP = -Wl,--wrap=sem_wait -Wl,--wrap=sem_trywait -Wl,--wrap=sem_timedwait -Wl,--wrap=sem_post -Wl,--wrap=sem_getvalue -Wl,--wrap=ffi_prep_closure_loc

# bsdtar and bsdunzip of libarchive (BSD licence): tar, zip, gzip and more in one library; with zlib. Built with cmake out of tree. Nothing else is switched
# on (no zstd, OpenSSL, xattrs, ACLs: they are libraries that are not here). make ARCHIVE=0 leaves them out.
LIBARCHIVE_OPTS = -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_TOOLCHAIN_FILE=$(abspath musl.cmake) -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_C_FLAGS=-Os \
  -DENABLE_TEST=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=ON -DENABLE_TAR=ON -DENABLE_OPENSSL=OFF -DENABLE_NETTLE=OFF -DENABLE_MBEDTLS=OFF \
  -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_LZMA=ON -DENABLE_ZSTD=OFF -DENABLE_BZip2=ON -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF \
  -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_ICONV=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_CNG=OFF -DENABLE_LIBGCC=OFF \
  -DENABLE_ZLIB=ON -DZLIB_ROOT=$(SYSROOT)
$(B)/bsdtar.elf: $(SYSROOT)/lib/libz.a $(SYSROOT)/lib/libbz2.a $(SYSROOT)/lib/liblzma.a musl.cmake musl.ld ../tools/mark_musl.py
	mkdir -p $(B)/libarchive
	cd $(B)/libarchive && MUSL_LDSCRIPT=$(abspath musl.ld) cmake -S $(THIRD)/libarchive -B . $(LIBARCHIVE_OPTS) > configure.log 2>&1
	cd $(B)/libarchive && MUSL_LDSCRIPT=$(abspath musl.ld) cmake --build . --target bsdtar bsdunzip -j4 > make.log 2>&1
	$(CROSS)strip -o $(B)/bsdtar.elf $(B)/libarchive/bin/bsdtar
	$(CROSS)strip -o $(B)/bsdunzip.elf $(B)/libarchive/bin/bsdunzip
	python3 ../tools/mark_musl.py $(B)/bsdtar.elf
	python3 ../tools/mark_musl.py $(B)/bsdunzip.elf
$(B)/bsdunzip.elf: $(B)/bsdtar.elf

# ncurses (for the Python module curses): the wide-character version, static. There are no terminfo files on the disk: the descriptions of a few
# terminals are compiled in (--with-fallbacks). Making that table needs a tic and an infocmp that run on the host, with the same options, so a
# host copy is built first (ncurses-host). The host's infocmp writes `short` for the numbers, the target wants `int`: sed repairs the generated table.
NCURSES_TERMS = linux,vt100,vt220,xterm,xterm-256color,ansi,screen,dumb
$(SYSROOT)/lib/libncursesw.a: $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/ncurses-host $(B)/ncurses
	cd $(B)/ncurses-host && $(THIRD)/ncurses/configure --without-shared --without-debug --enable-widec --without-cxx --without-cxx-binding --without-ada \
	  --without-manpages --without-tests > configure.log 2>&1 && $(MAKE) -C include > make.log 2>&1 && $(MAKE) -C ncurses >> make.log 2>&1 \
	  && $(MAKE) -C progs tic infocmp >> make.log 2>&1
	cd $(B)/ncurses && MUSL_LDSCRIPT=$(abspath musl.ld) CC=$(abspath musl-cc) CPP="$(abspath musl-cc) -E" AR=$(CROSS)ar RANLIB=$(CROSS)ranlib CFLAGS=-Os \
	  $(THIRD)/ncurses/configure --host=aarch64-linux-musl --build=$$($(CPYTHON)/config.guess) --prefix=$(SYSROOT) --without-shared --with-normal --without-debug \
	  --enable-widec --enable-overwrite --without-cxx --without-cxx-binding --without-ada --without-manpages --without-tests --without-progs --disable-database \
	  --with-fallbacks=$(NCURSES_TERMS) --with-tic-path=$(abspath $(B)/ncurses-host/progs/tic) --with-infocmp-path=$(abspath $(B)/ncurses-host/progs/infocmp) \
	  --without-gpm --disable-stripping --disable-leaks --without-pthread --disable-home-terminfo --with-build-cc=cc > configure.log 2>&1
	cd $(B)/ncurses && { $(MAKE) > make.log 2>&1 || { sed -i.bak 's/^static short \(.*_number_data\)/static int \1/' ncurses/fallback.c && $(MAKE) > make.log 2>&1; }; }
	cd $(B)/ncurses && $(MAKE) install.libs install.includes > install.log 2>&1

# OpenSSL 3.0 (Apache-2.0), for the Python modules ssl and hashlib's _hashlib (and everything else that wants the OpenSSL interface; the
# programs of the system that use TLS take BearSSL). Static, no assembler (portable C), no engines, no dlopen, no ktls. The certificate
# authorities are read from /etc/ssl/cacert.pem (--openssldir=/etc/ssl; ssl.create_default_context finds cert.pem there, see python-setup.local).
$(SYSROOT)/lib/libssl.a: $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/openssl
	cd $(B)/openssl && CC=$(abspath musl-cc) AR=$(CROSS)ar RANLIB=$(CROSS)ranlib MUSL_LDSCRIPT=$(abspath musl.ld) perl $(THIRD)/openssl/Configure linux-aarch64 \
	  no-shared no-asm no-dso no-engine no-afalgeng no-async no-tests no-secure-memory no-ktls no-ssl3 no-comp --prefix=$(SYSROOT) --openssldir=/etc/ssl -Os > configure.log 2>&1
	cd $(B)/openssl && $(MAKE) build_libs -j4 > make.log 2>&1 && $(MAKE) install_dev > install.log 2>&1

# bzip2 (BSD-style licence): seven C files, compiled here (its own Makefile builds in the source tree). For the Python module bz2 and for libarchive (tar -j).
BZ2_SRC = blocksort.c bzlib.c compress.c crctable.c decompress.c huffman.c randtable.c
$(SYSROOT)/lib/libbz2.a: $(SYSROOT)/lib/libc.a
	mkdir -p $(B)/bzip2
	for f in $(BZ2_SRC); do ./musl-cc -c -Os -D_FILE_OFFSET_BITS=64 -o $(B)/bzip2/$$f.o $(THIRD)/bzip2/$$f || exit 1; done
	$(CROSS)ar rcs $@ $(B)/bzip2/*.o
	cp $(THIRD)/bzip2/bzlib.h $(SYSROOT)/include/

# xz's liblzma (public domain / 0BSD) for the Python module lzma and for libarchive (tar -J). Version 5.4.7: NOT 5.6.0 or 5.6.1 (the backdoored releases).
$(SYSROOT)/lib/liblzma.a: $(SYSROOT)/lib/libc.a musl.cmake
	mkdir -p $(B)/xz
	cd $(B)/xz && MUSL_LDSCRIPT=$(abspath musl.ld) cmake -S $(THIRD)/xz -B . -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_TOOLCHAIN_FILE=$(abspath musl.cmake) \
	  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_C_FLAGS=-Os -DXZ_NLS=OFF -DXZ_DOC=OFF > configure.log 2>&1
	cd $(B)/xz && cmake --build . --target liblzma -j4 > make.log 2>&1
	cp $(B)/xz/liblzma.a $@
	cp -R $(THIRD)/xz/src/liblzma/api/lzma.h $(THIRD)/xz/src/liblzma/api/lzma $(SYSROOT)/include/

# GNU readline 8.2 (GPLv3+, which is the licence of this project) with the ncurses above, for the Python module readline (line editing and history)
$(SYSROOT)/lib/libreadline.a: $(SYSROOT)/lib/libncursesw.a
	mkdir -p $(B)/readline
	cd $(B)/readline && MUSL_LDSCRIPT=$(abspath musl.ld) CC=$(abspath musl-cc) AR=$(CROSS)ar RANLIB=$(CROSS)ranlib CFLAGS="-Os -std=gnu99" \
	  bash_cv_wcwidth_broken=no bash_cv_func_sigsetjmp=present bash_cv_func_ctype_nonascii=no bash_cv_must_reinstall_sighandlers=no \
	  bash_cv_func_strcoll_broken=no bash_cv_func_snprintf=yes bash_cv_func_vsnprintf=yes bash_cv_getcwd_malloc=yes bash_cv_job_control_missing=present \
	  bash_cv_sys_named_pipes=present bash_cv_unusable_rtsigs=no $(THIRD)/readline/configure --host=aarch64-linux-musl --build=$$($(CPYTHON)/config.guess) \
	  --prefix=$(SYSROOT) --disable-shared --enable-static --with-curses > configure.log 2>&1
	cd $(B)/readline && $(MAKE) static > make.log 2>&1 && $(MAKE) install-static install-headers > install.log 2>&1

# (no unlinkat: the kernel does not know the path of an open directory, so shutil.rmtree must use paths and not directory descriptors;
#  no epoll: the kernel has none, and selectors.DefaultSelector would pick it and asyncio would fail with ENOSYS; without it the poll selector is used)
$(B)/python/Makefile: $(SYSROOT)/lib/libc.a $(SYSROOT)/lib/libz.a $(SYSROOT)/lib/libsqlite3.a $(SYSROOT)/lib/libffi.a $(SYSROOT)/lib/libncursesw.a $(SYSROOT)/lib/libssl.a $(SYSROOT)/lib/libbz2.a $(SYSROOT)/lib/liblzma.a $(SYSROOT)/lib/libreadline.a musl-cc musl.ld python-setup.local Makefile
	mkdir -p $(B)/python/Modules
	cp python-setup.local $(B)/python/Modules/Setup.local          # (configure leaves an existing Setup.local alone)
	cd $(B)/python && MUSL_LDSCRIPT=$(abspath musl.ld) CC=$(abspath musl-cc) CPP="$(abspath musl-cc) -E" AR=$(CROSS)ar RANLIB=$(CROSS)ranlib \
	  READELF=$(CROSS)readelf $(CPYTHON)/configure --host=aarch64-linux-musl --build=$$($(CPYTHON)/config.guess) \
	  --with-build-python=$(BUILD_PYTHON) --disable-shared --without-ensurepip --disable-ipv6 \
	  ac_cv_file__dev_ptmx=no ac_cv_file__dev_ptc=no ac_cv_func_epoll_create=no ac_cv_func_epoll_create1=no ac_cv_func_unlinkat=no > configure.log 2>&1

# (dlundef.ld names every function of libc.a, so that ctypes can call all of them, not only the ones that Python itself uses: 150 KByte more)
PYLINK = MUSL_LDSCRIPT=$(abspath musl.ld) $(MAKE) -C $(B)/python python.exe LDFLAGS_NODIST="$(abspath $(B)/dl/dlshim.o) $(abspath $(B)/dl/semshim.o) $(abspath $(B)/dl/dlsyms.o) $(abspath $(B)/dl/dlundef.ld) $(SEMWRAP)"
$(B)/python.elf: $(B)/python/Makefile $(B)/dl/dlshim.o $(B)/dl/semshim.o FORCE ../tools/mark_musl.py gen_dlsyms.py
	python3 gen_dlsyms.py --extern $(CROSS)nm $(SYSROOT)/lib/libc.a > $(B)/dl/dlundef.ld
	python3 gen_dlsyms.py - > $(B)/dl/dlsyms.c && ./musl-cc -c -O1 -o $(B)/dl/dlsyms.o $(B)/dl/dlsyms.c
	$(PYLINK) > $(B)/python/make.log 2>&1
	python3 gen_dlsyms.py $(CROSS)readelf $(B)/python/python.exe > $(B)/dl/dlsyms.c && ./musl-cc -c -O1 -o $(B)/dl/dlsyms.o $(B)/dl/dlsyms.c
	$(RM) $(B)/python/python.exe
	$(PYLINK) >> $(B)/python/make.log 2>&1
	$(CROSS)strip -o $@ $(B)/python/python.exe
	python3 ../tools/mark_musl.py $@

FORCE:

clean:
	rm -rf $(B)
