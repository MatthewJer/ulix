# The C++ standard library for programs linked with musl (issue #43, section "The C++ library" of lit/libc.nw, section 50 of ../ARM64-PORT.md).
# LLVM's libc++ (the library), libc++abi (throw, catch, typeinfo, new) and libunwind (the stack walk), sources unmodified in
# ../../third_party/llvm-project. No CMake: every source file is compiled with the same few flags, the lists below are those of the CMakeLists.txt files
# for "threads off, filesystem off, no shared library" (see the licence check in ../../third_party/LICENCE-CHECK-llvm.md).
# Included by libc/Makefile; installs libc++.a (with libc++abi), libunwind.a and the headers into the sysroot.

LLVM    = $(THIRD)/llvm-project
CXXB    = $(B)/libcxx
CXXINC  = $(CXXB)/include
LIBCXX_SRC = algorithm any bind call_once charconv chrono error_category exception expected functional hash memory memory_resource \
             new_handler new_helpers optional print random_shuffle ryu/d2fixed ryu/d2s ryu/f2s stdexcept string system_error typeinfo \
             valarray variant vector verbose_abort fstream ios ios.instantiations iostream locale ostream regex strstream
CXXABI_SRC = abort_message cxa_aux_runtime cxa_default_handlers cxa_demangle cxa_exception cxa_exception_storage cxa_guard cxa_handlers \
             cxa_personality cxa_vector cxa_virtual fallback_malloc private_typeinfo stdlib_exception stdlib_new_delete stdlib_stdexcept stdlib_typeinfo
UNWIND_C   = UnwindLevel1 UnwindLevel1-gcc-ext
UNWIND_CXX = libunwind
UNWIND_S   = UnwindRegistersRestore UnwindRegistersSave

CXXCOMMON = -D_GNU_SOURCE -O1 -g0 -fno-pic -mno-outline-atomics -nostdinc -nostdinc++ -w -funwind-tables
CXXHDRS   = -isystem $(CXXINC) -isystem $(LLVM)/libcxx/include -isystem $(SYSROOT)/include -isystem $(shell $(CC) -print-file-name=include)
LIBCXX_FLAGS  = $(CXXCOMMON) -std=c++23 $(CXXHDRS) -I$(LLVM)/libcxx/src -I$(LLVM)/libcxxabi/include -I$(LLVM)/libcxxabi/src -I$(LLVM)/libc \
                -D_LIBCPP_BUILDING_LIBRARY -DLIBCXX_BUILDING_LIBCXXABI -D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
CXXABI_FLAGS  = $(CXXCOMMON) -std=c++23 $(CXXHDRS) -I$(LLVM)/libcxxabi/include -I$(LLVM)/libcxx/src -I$(LLVM)/libunwind/include \
                -D_LIBCXXABI_BUILDING_LIBRARY -D_LIBCXXABI_HAS_NO_THREADS -D_LIBCPP_BUILDING_LIBRARY -D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
UNWIND_FLAGS  = $(CXXCOMMON) -I$(LLVM)/libunwind/include -D_LIBUNWIND_IS_NATIVE_ONLY -D_LIBUNWIND_HAS_NO_THREADS=1 -D_LIBUNWIND_USE_DLADDR=0 -D_LIBUNWIND_REMEMBER_STACK_ALLOC -fno-exceptions

# the file that CMake would make from __config_site.in: one switch per feature
$(CXXINC)/__config_site: libcxx_config_site.h
	mkdir -p $(CXXINC)
	cp $< $@
	cp $(LLVM)/libcxx/vendor/llvm/default_assertion_handler.in $(CXXINC)/__assertion_handler

$(CXXB)/cxx-%.o: $(LLVM)/libcxx/src/%.cpp $(CXXINC)/__config_site
	mkdir -p $(dir $@)
	$(CROSS)g++ $(LIBCXX_FLAGS) -c -o $@ $<
$(CXXB)/abi-%.o: $(LLVM)/libcxxabi/src/%.cpp $(CXXINC)/__config_site
	mkdir -p $(dir $@)
	$(CROSS)g++ $(CXXABI_FLAGS) -c -o $@ $<
$(CXXB)/unw-%.o: $(LLVM)/libunwind/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(UNWIND_FLAGS) -nostdinc -isystem $(SYSROOT)/include -isystem $(shell $(CC) -print-file-name=include) -c -o $@ $<
$(CXXB)/unw-%.o: $(LLVM)/libunwind/src/%.cpp
	mkdir -p $(dir $@)
	$(CROSS)g++ $(UNWIND_FLAGS) -fno-rtti -std=c++17 -isystem $(LLVM)/libcxx/include -isystem $(CXXINC) -isystem $(SYSROOT)/include -isystem $(shell $(CC) -print-file-name=include) -c -o $@ $<
$(CXXB)/unw-%.o: $(LLVM)/libunwind/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(UNWIND_FLAGS) -c -o $@ $<

LIBCXX_OBJS = $(addprefix $(CXXB)/cxx-,$(addsuffix .o,$(LIBCXX_SRC))) $(addprefix $(CXXB)/abi-,$(addsuffix .o,$(CXXABI_SRC)))
UNWIND_OBJS = $(addprefix $(CXXB)/unw-,$(addsuffix .o,$(UNWIND_C) $(UNWIND_CXX) $(UNWIND_S)))

$(SYSROOT)/lib/libc++.a: $(LIBCXX_OBJS) $(SYSROOT)/lib/libc.a
	$(RM) $@
	$(CROSS)ar rcs $@ $(LIBCXX_OBJS)
$(SYSROOT)/lib/libunwind.a: $(UNWIND_OBJS)
	$(RM) $@
	$(CROSS)ar rcs $@ $(UNWIND_OBJS)
