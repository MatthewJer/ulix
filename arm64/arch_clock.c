// Ulix ARM64 -- the clock.  The x86 kernel reads the CMOS clock once at boot (system_start_time) and counts
// timer ticks; the book's timer handler then does
//     system_ticks++;  system_time = system_ticks / 100 + system_start_time;
// Here the start time comes from the PL031 real-time clock of the board (seconds since 1 January 1970),
// the tick counting is the same, driven by the generic timer at 100 Hz.

#include "ulix-api.h"

extern unsigned long plat_rtc (void);                                          // from the device tree
#define RTC_DR  (*(volatile unsigned int *)(0xffff000000000000UL + plat_rtc ()))     // data register: seconds

// A board without a real-time clock (the Raspberry Pi 4B has none) starts at this date: 1 January 2026, 00:00 UTC.
#define NO_CLOCK_START_TIME 1767225600U

void arch_clock_init (void) {
  system_start_time = plat_rtc () ? RTC_DR : NO_CLOCK_START_TIME;
  system_ticks = 0;
  system_time  = system_start_time;
}

void arch_clock_tick (void) {                 // called from the timer interrupt, 100 times a second
  system_ticks++;
  system_time = (uint)(system_ticks / 100) + system_start_time;
}
