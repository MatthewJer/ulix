# Licence check: LLVM runtimes (libc++, libc++abi, libunwind)
- Name:         LLVM `libcxx`, `libcxxabi`, `libunwind` (the C++ standard library for the AArch64 user space, issue #43)
- Upstream URL: https://github.com/llvm/llvm-project (shallow submodule `third_party/llvm-project`, only these three
                directories and `cmake`, `runtimes`, `llvm/cmake`, `llvm/utils/llvm-lit` are needed; a sparse checkout)
- Tag:          not pinned yet: a `llvmorg-<N>.<M>.<K>` release tag has to be chosen when the submodule is added
                (STATUS: the sources were NOT fetched yet, this file is a draft, see below)
- Licence:      Apache-2.0 with LLVM exceptions (SPDX `Apache-2.0 WITH LLVM-exception`), see `libcxx/LICENSE.TXT`,
                `libcxxabi/LICENSE.TXT`, `libunwind/LICENSE.TXT`; every source file carries that line in its header.
                (Before LLVM 9 the licence was MIT plus UIUC; a release tag from LLVM 9 on is used.)
- Why it is compatible with GPLv3: Apache-2.0 may be combined with GPLv3 (it is not compatible with GPLv2, which
  does not matter here); the LLVM exception only loosens the terms: object code that is linked with the runtime
  needs no attribution notice. Notices in the sources are kept.
- Other licences inside: to be confirmed when the tree is checked out (known: `libcxx/src/include/ryu/` is Apache-2.0
  with LLVM exception OR Boost Software Licence 1.0, both permissive; `libcxx/src/include/from_chars_floating_point.h`
  likewise; `libcxxabi/src/demangle` is the same code as `llvm/Demangle`, Apache-2.0 with exception).
- Linux code in it? No (libc++ has code for Linux system calls only through the C library, i.e. musl).
- Patches:      none planned; configuration only (`LIBCXX_HAS_MUSL_LIBC=ON`, `LIBCXX_ENABLE_THREADS=OFF`, ...)
