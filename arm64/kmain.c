// Ulix ARM64 -- first kernel code: PL011 console and a minimal printf

typedef unsigned long      u64;
typedef unsigned int       u32;
typedef unsigned char      u8;

#define KERNEL_VA   0xFFFF000000000000UL
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

extern void irq_init (void);
extern void drop_identity_map (void);
extern void frames_init (void);
extern u64  alloc_frame (void);
extern void free_frame (u64 pa);
extern int  map_page (u64 va, u64 pa, u64 flags);
extern u64  unmap_page (u64 va);
extern u64  virt_to_phys (u64 va);
#define KVA(pa)   ((void *)(KERNEL_VA + (u64)(pa)))
#define PAGE_RW   ((3UL) | (1UL << 2) | (3UL << 8) | (1UL << 10) | (1UL << 53) | (1UL << 54))
#define PAGE_RO   (PAGE_RW | (1UL << 7))

void kmain (void) {
  u64 el;
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (el));
  printf ("Ulix-arm64 (work in progress)\n");
  printf ("Running at EL%d, sizeof(void*) = %d, sizeof(long) = %d\n",
          (int)(el >> 2), (int)sizeof (void *), (int)sizeof (long));
  printf ("kmain at 0x%lx\n", (u64)kmain);
  irq_init ();
  extern volatile u64 ticks;
  u64 last = 0;
  while (ticks < 300) {                  // 3 seconds
    __asm__ volatile ("wfi");
    if (ticks / 100 != last / 100) printf ("tick %lu (%lu s)\n", ticks, ticks / 100);
    last = ticks;
  }
  printf ("testing synchronous exceptions: svc #0\n");
  __asm__ volatile ("svc #0");
  printf ("back from svc\n");

  // ---- stage 4: MMU ----
  drop_identity_map ();
  printf ("MMU on, running at 0x%lx (upper half), identity map dropped\n", (u64)kmain);
  frames_init ();

  const u64 va = 0xFFFF008000000000UL;           // not covered by the boot map
  extern volatile int expect_fault, faults;
  extern volatile u64 fault_esr, fault_far;
  int fails = 0;
  #define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                                else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

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

  printf (fails ? "MMU test: %d FAILED\n" : "MMU test: all passed\n", fails);
  for (;;) __asm__ volatile ("wfe");
}
