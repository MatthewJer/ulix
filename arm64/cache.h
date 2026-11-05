// Ulix ARM64 -- the CPU cache and DMA: what a device reads must first be written back from the cache to memory, and what a
// device has written must be read again from memory (the cache may hold an old copy). qemu's memory is coherent, so these do
// nothing visible there; on the Raspberry Pi they matter.
#ifndef CACHE_H
#define CACHE_H
#include "ulix.h"

static inline void cache_clean (const void *p, u64 len) {                 // before the device reads: write back to memory
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc cvac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}

static inline void cache_flush_inval (const void *p, u64 len) {           // before the CPU reads what the device wrote: forget the cached copy
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc civac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}
#endif
