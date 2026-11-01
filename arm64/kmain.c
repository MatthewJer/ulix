// Ulix ARM64 -- kernel entry, PL011 console, minimal printf

#include "ulix.h"


#define UART0_BASE  (KERNEL_VA + 0x09000000UL)   // PL011 on qemu "virt", via the linear map
#define UART_DR     (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR     (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1 << 5)

void putch (char c) {
  if (c == '\n') putch ('\r');
  while (UART_FR & UART_FR_TXFF) ;
  UART_DR = c;
}

static void puts_ (const char *s) { while (*s) putch (*s++); }

static void putnum (u64 v, int base, int width, char pad) {
  char buf[32]; int n = 0;
  do { buf[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
  while (width > n) { putch (pad); width--; }
  while (n) putch (buf[--n]);
}

// supports %s %c %d %u %x %lx %% with optional 0-padding / width
void printf (const char *fmt, ...) {
  __builtin_va_list ap;
  __builtin_va_start (ap, fmt);
  for (; *fmt; fmt++) {
    if (*fmt != '%') { putch (*fmt); continue; }
    fmt++;
    char pad = ' '; int width = 0, lng = 0;
    if (*fmt == '0') { pad = '0'; fmt++; }
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    if (*fmt == 'l') { lng = 1; fmt++; }
    switch (*fmt) {
      case 's': puts_ (__builtin_va_arg (ap, const char *)); break;
      case 'c': putch ((char)__builtin_va_arg (ap, int)); break;
      case 'd': {
        long v = lng ? __builtin_va_arg (ap, long) : __builtin_va_arg (ap, int);
        if (v < 0) { putch ('-'); v = -v; }
        putnum (v, 10, width, pad); break; }
      case 'u': putnum (lng ? __builtin_va_arg (ap, u64) : __builtin_va_arg (ap, u32), 10, width, pad); break;
      case 'x': putnum (lng ? __builtin_va_arg (ap, u64) : __builtin_va_arg (ap, u32), 16, width, pad); break;
      case '%': putch ('%'); break;
      default:  putch ('%'); putch (*fmt); break;
    }
  }
  __builtin_va_end (ap);
}


extern volatile u64 ticks;

#define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                              else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

static int fails = 0;

static void mmu_selftest (void) {
  const u64 va = 0xFFFF008000000000UL;           // not covered by the boot map
  u64 pa = alloc_frame ();
  CHECK (map_page (va, pa, PAGE_RW) == 0, "map_page (RW)");
  CHECK (virt_to_phys (va + 0x123) == pa + 0x123, "AT translation matches");
  *(volatile u64 *)va = 0x1122334455667788UL;
  CHECK (*(volatile u64 *)KVA (pa) == 0x1122334455667788UL, "write via mapping visible via linear map");
  CHECK (map_page (va, pa, PAGE_RW) == -1, "double map rejected");

  CHECK (unmap_page (va) == pa, "unmap_page returns frame");
  expect_fault = 1; faults = 0;
  (void)*(volatile u64 *)va;
  expect_fault = 0;
  CHECK (faults == 1 && fault_far == va && (fault_esr & 0x3f) == 0x07,
         "read of unmapped page -> translation fault level 3");

  CHECK (map_page (va, pa, PAGE_RO) == 0, "map_page (RO)");
  expect_fault = 1; faults = 0;
  *(volatile u64 *)va = 1;
  expect_fault = 0;
  CHECK (faults == 1 && (fault_esr & 0x3f) == 0x0f, "write to RO page -> permission fault level 3");
  CHECK (*(volatile u64 *)va == 0x1122334455667788UL, "RO page still readable, data intact");

  unmap_page (va);
  free_frame (pa);
  CHECK (alloc_frame () == pa, "freed frame is reused (and zeroed)");
  free_frame (pa);
}

static void process_test (void) {
  long frames_before = frames_in_use;
  printf ("starting 3 user processes: hello 1, hello 2, bad\n");
  int p1 = task_create_user (user_hello_start, user_hello_end, 1);
  int p2 = task_create_user (user_hello_start, user_hello_end, 2);
  int p3 = task_create_user (user_bad_start,   user_bad_end,   0);
  CHECK (p1 == 1 && p2 == 2 && p3 == 3, "three processes created with pids 1, 2, 3");

  while (tasks_live ()) {                         // idle loop
    tasks_reap ();
    if (tasks_live ()) __asm__ volatile ("wfi");
  }
  tasks_reap ();

  CHECK (sys_write_calls == 10, "10 write syscalls from the two hello processes");
  CHECK (procs_killed == 1, "bad process killed by the kernel (permission fault), kernel survived");
  CHECK (tasks_reaped == 3, "all three processes reaped");
  CHECK (preemptions >= 1, "a running process was preempted by the timer interrupt");
  printf ("  info: %d context switches (%d by timer), %lu timer ticks\n", context_switches, preemptions, ticks);
  CHECK (frames_in_use == frames_before, "no frame leaked: address spaces and kernel stacks freed");
}

void kmain (void) {
  u64 el;
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (el));
  printf ("Ulix-arm64 (work in progress)\n");
  printf ("Running at EL%d, sizeof(void*) = %d, kmain at 0x%lx (upper half)\n",
          (int)(el >> 2), (int)sizeof (void *), (u64)kmain);
  irq_init ();
  __asm__ volatile ("svc #0");                    // kernel-mode svc: handler just reports it

  frames_init ();
  user_mm_init ();                                // TTBR0 is now free for user processes
  printf ("MMU self-test:\n");
  mmu_selftest ();

  sched_init ();
  printf ("process test:\n");
  process_test ();

  printf (fails ? "RESULT: %d check(s) FAILED\n" : "RESULT: all checks passed\n", fails);
  for (;;) __asm__ volatile ("wfe");
}
