#!/bin/sh
# Fetches the C++ library sources (libc++, libc++abi, libunwind) of LLVM into third_party/llvm-project: a sparse, shallow checkout of the tag
# the repository uses (about 40 MByte of git objects instead of several GByte). Run from anywhere; see third_party/LICENCE-CHECK-llvm.md.
set -e
top=$(cd "$(dirname "$0")/../.." && pwd)
dir=$top/third_party/llvm-project
tag=llvmorg-21.1.8
if test -e "$dir/libcxx/include"; then echo "third_party/llvm-project is already there"; exit 0; fi
rmdir "$dir" 2>/dev/null || true       # an empty directory of an uninitialised submodule
git clone --depth 1 --filter=blob:none --sparse --branch "$tag" https://github.com/llvm/llvm-project.git "$dir"
git -C "$dir" sparse-checkout set --no-cone /libcxx/ /libcxxabi/ /libunwind/ /cmake/ /runtimes/ /llvm/cmake/ /llvm/LICENSE.TXT /third-party/ /libc/shared/ /libc/src/__support/ /libc/include/ /libc/hdr/
