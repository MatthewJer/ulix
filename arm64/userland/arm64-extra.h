// What the AArch64 build adds to programs of lib-build/tools that were written for a 64 MByte PC.
// (Only the programs that need it include this file; see the rule for free.c in the Makefile.)
#ifndef ARM64_EXTRA_H
#define ARM64_EXTRA_H
#define __NR_total_frames 601                       // AArch64-only system call: frames (4 KByte) of RAM
static inline int total_frames (void) { return syscall1 (__NR_total_frames); }
#endif
