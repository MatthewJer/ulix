// Ulix ARM64 -- waiting a number of microseconds (for the drivers of hardware that needs a moment)
#ifndef DELAY_H
#define DELAY_H
#include "ulix.h"

// The counter of the generic timer runs at a fixed frequency, whatever the CPU does.
static inline void delay_us (u32 us) {
  u64 freq, start, now;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (start));
  do __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (now)); while ((now - start) * 1000000 / freq < us);
}
#endif
