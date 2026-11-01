// Ulix ARM64 -- first kernel code: PL011 console and a minimal printf

typedef unsigned long      u64;
typedef unsigned int       u32;
typedef unsigned char      u8;

#define UART0_BASE  0x09000000UL      // PL011 on qemu "virt"
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
  printf ("testing synchronous exceptions: svc #0 (returns), then brk #0 (halts)\n");
  __asm__ volatile ("svc #0");
  printf ("back from svc\n");
  __asm__ volatile ("brk #0");
  for (;;) __asm__ volatile ("wfe");
}
