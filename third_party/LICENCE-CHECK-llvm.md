# Licence check: LLVM runtimes (libc++, libc++abi, libunwind)
- Name:         LLVM `libcxx`, `libcxxabi`, `libunwind` (the C++ standard library for the AArch64 user space, issue #43)
- Upstream URL: https://github.com/llvm/llvm-project (shallow submodule `third_party/llvm-project`, only these three
                directories and `cmake`, `runtimes`, `llvm/cmake`, `llvm/utils/llvm-lit` are needed; a sparse checkout)
- Tag:          llvmorg-21.1.8 (commit 2078da43e25a4623cab2d0d60decddf709aaea28), shallow, unmodified. Sparse checkout
                (about 130 MByte in the working tree, 38 MByte of git objects instead of several GByte):
                `git -C third_party/llvm-project sparse-checkout set --no-cone /libcxx/ /libcxxabi/ /libunwind/ /cmake/ /runtimes/ /llvm/cmake/ /llvm/LICENSE.TXT /third-party/ /libc/shared/ /libc/src/__support/ /libc/include/ /libc/hdr/`
- Licence:      Apache-2.0 with LLVM exceptions (SPDX `Apache-2.0 WITH LLVM-exception`), see `libcxx/LICENSE.TXT`,
                `libcxxabi/LICENSE.TXT`, `libunwind/LICENSE.TXT`; every source file carries that line in its header.
                (Before LLVM 9 the licence was MIT plus UIUC; a release tag from LLVM 9 on is used.)
- Why it is compatible with GPLv3: Apache-2.0 may be combined with GPLv3 (it is not compatible with GPLv2, which
  does not matter here); the LLVM exception only loosens the terms: object code that is linked with the runtime
  needs no attribution notice. Notices in the sources are kept.
- Other licences inside: checked in the tree: every source file under `libcxx/src`, `libcxxabi/src`, `libunwind/src` carries
  `SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception` (the files without it are CMake files, READMEs and `.clang-*`);
  `libcxx/src/include/ryu/` is from Microsoft's STL port of Ryu (Ulf Adams): Apache-2.0 with LLVM exception or Boost
  Software Licence 1.0, both permissive. No GPL text in the three directories except mentions in documentation.
- Linux code in it? No (libc++ has code for Linux system calls only through the C library, i.e. musl).
- Patches:      none planned; configuration only (`LIBCXX_HAS_MUSL_LIBC=ON`, `LIBCXX_ENABLE_THREADS=OFF`, ...)
