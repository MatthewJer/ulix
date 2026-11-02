// Ulix ARM64 -- kernel entry, PL011 console, minimal printf

#include "ulix.h"
#include "disk_expected.h"       // sizes and checksums of files on the disk, from the independent Python parser


#define UART0_BASE  (KERNEL_VA + 0x09000000UL)   // PL011 on qemu "virt", via the linear map
#define UART_DR     (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR     (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1 << 5)

static char cap_buf[8192];
static int  cap_len = 0, cap_on = 0;
void capture_start (void) { cap_len = 0; cap_on = 1; }
int  capture_stop (char **text) { cap_on = 0; cap_buf[cap_len] = 0; *text = cap_buf; return cap_len; }

void putch (char c) {
  if (cap_on && cap_len < (int)sizeof cap_buf - 1) cap_buf[cap_len++] = c;
  if (c == '\n') putch ('\r');
  while (UART_FR & UART_FR_TXFF) ;
  UART_DR = c;
}

// ---- formatted output: one formatter for printf (console) and sprintf (memory) ----
struct sink { char *buf; int n; };                 // buf == 0: write to the console

static void emit (struct sink *s, char c) {
  if (s->buf) s->buf[s->n++] = c;
  else        putch (c);
}

static void emit_padded (struct sink *s, const char *text, int len, int width, int left, char pad) {
  if (!left) for (int i = len; i < width; i++) emit (s, pad);
  for (int i = 0; i < len; i++) emit (s, text[i]);
  if (left)  for (int i = len; i < width; i++) emit (s, ' ');
}

static void emit_num (struct sink *s, u64 v, int base, int neg, int width, int left, char pad) {
  char t[32]; int n = 0;
  do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
  if (neg) t[n++] = '-';
  char r[32];
  for (int i = 0; i < n; i++) r[i] = t[n - 1 - i];
  emit_padded (s, r, n, width, left, pad);
}

// supports %s %c %d %u %x %p %% with the flags '-' and '0', a width, and the length modifier 'l'
static void vformat (struct sink *s, const char *fmt, __builtin_va_list ap) {
  for (; *fmt; fmt++) {
    if (*fmt != '%') { emit (s, *fmt); continue; }
    fmt++;
    char pad = ' '; int width = 0, lng = 0, left = 0;
    for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') pad = '0'; else break; }
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    while (*fmt == 'l') { lng = 1; fmt++; }
    switch (*fmt) {
      case 's': { const char *t = __builtin_va_arg (ap, const char *); if (!t) t = "(null)";
                  int len = 0; while (t[len]) len++;
                  emit_padded (s, t, len, width, left, ' '); break; }
      case 'c': { char c = (char)__builtin_va_arg (ap, int); emit_padded (s, &c, 1, width, left, ' '); break; }
      case 'd': { long v = lng ? __builtin_va_arg (ap, long) : __builtin_va_arg (ap, int);
                  emit_num (s, v < 0 ? -(u64)v : (u64)v, 10, v < 0, width, left, pad); break; }
      case 'u': emit_num (s, lng ? __builtin_va_arg (ap, u64) : __builtin_va_arg (ap, u32), 10, 0, width, left, pad); break;
      case 'x': emit_num (s, lng ? __builtin_va_arg (ap, u64) : __builtin_va_arg (ap, u32), 16, 0, width, left, pad); break;
      case 'p': emit_num (s, __builtin_va_arg (ap, u64), 16, 0, width, left, pad); break;
      case '%': emit (s, '%'); break;
      case 0:   return;
      default:  emit (s, '%'); emit (s, *fmt); break;
    }
  }
}

int printf (const char *fmt, ...) {
  struct sink s = { 0, 0 };
  __builtin_va_list ap;
  __builtin_va_start (ap, fmt);
  vformat (&s, fmt, ap);
  __builtin_va_end (ap);
  return 0;
}

int sprintf (char *out, const char *fmt, ...) {
  struct sink s = { out, 0 };
  __builtin_va_list ap;
  __builtin_va_start (ap, fmt);
  vformat (&s, fmt, ap);
  __builtin_va_end (ap);
  out[s.n] = 0;
  return s.n;
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

static u32 file_sum (const char *path, u32 *size_out) {
  static u8 buf[65536] __attribute__ ((aligned (16)));
  u32 ino = minix_lookup (path), sum = 0;
  if (!ino) { *size_out = 0; return 0; }
  long n = minix_read (ino, 0, buf, sizeof buf);
  *size_out = n < 0 ? 0 : n;
  for (long i = 0; i < n; i++) sum += buf[i];
  return sum;
}

static void count_cb (u32 ino, const char *name, void *arg) { (*(int *)arg)++; }

static void disk_test (void) {
  static u8 a[1024] __attribute__ ((aligned (16))), b[1024] __attribute__ ((aligned (16)));
  CHECK (blk_init () == 0, "virtio-blk device found and initialised");
  CHECK (blk_capacity () == 8192, "capacity is 8192 sectors (4 MByte: x86 image grown by tools/minix_add.py)");

  CHECK (blk_read (2, a, 2) == 0 && a[16] == 0x78 && a[17] == 0x24, "superblock read, Minix v2 magic 0x2478");
  CHECK (blk_read (blk_capacity (), a, 1) == -1, "read beyond the end of the disk is rejected");

  for (int i = 0; i < 1024; i++) a[i] = (u8)(i * 7 + 3);
  CHECK (blk_write (2800, a, 2) == 0 && blk_read (2800, b, 2) == 0, "write 2 sectors and read them back");
  int same = 1;
  for (int i = 0; i < 1024; i++) if (a[i] != b[i]) same = 0;
  CHECK (same, "read-back data identical (disk is run with -snapshot, image file stays untouched)");

  CHECK (minix_mount () == 0, "Minix file system mounted");
  int n = 0;
  minix_readdir (1, count_cb, &n);
  CHECK (n == 27, "root directory has 27 entries (16 from x86 Ulix + 11 AArch64 files)");
  n = 0;
  minix_readdir (minix_lookup ("/bin"), count_cb, &n);
  CHECK (n == 63, "/bin has 63 entries");
  CHECK (minix_lookup ("/nonexistent") == 0, "unknown path not found");

  // reference values computed on the host from the same image with an independent parser
  u32 size, sum;
  sum = file_sum ("/hello.c", &size);
  CHECK (size == EXPECT_HELLO_C_SIZE && sum == EXPECT_HELLO_C_SUM, "/hello.c: size and checksum match");
  sum = file_sum ("/test.txt", &size);
  CHECK (size == EXPECT_TEST_TXT_SIZE && sum == EXPECT_TEST_TXT_SUM, "/test.txt: two zones, size and checksum match");
  sum = file_sum ("/init", &size);
  CHECK (size == EXPECT_INIT_SIZE && sum == EXPECT_INIT_SUM && size > 7 * 1024, "/init: more than 7 KByte (indirect zone), checksum matches");
  sum = file_sum ("/bin/vi", &size);
  CHECK (size == EXPECT_BIN_VI_SIZE && sum == EXPECT_BIN_VI_SUM && size > 7 * 1024, "/bin/vi: through a subdirectory, checksum matches");

  u8 text[64];
  long got = minix_read (minix_lookup ("/hello.c"), 0, text, 40);
  text[got > 0 ? got : 0] = 0;
  printf ("  info: /hello.c starts with: %s\n", text);
}

#ifdef CONSOLE_MODE
// Interactive mode (ulix-console.elf): no test suites. The kernel starts /contest64 and then
// waits; the UART is the console. Ctrl-C ends the foreground process, Ctrl-] opens the kernel shell.
extern void console_irq_init (void);
extern void arch_threads_init (void);
extern int  arch_start_user (const char *path, int argc, char **argv);

static void console_main (void) {
  frames_init ();
  user_mm_init ();
  blk_init ();
  minix_mount ();
  arch_threads_init ();
  console_irq_init ();
  printf ("console ready (Ctrl-C: end the running program, Ctrl-]: kernel shell)\n");
#ifndef CONSOLE_PROGRAM
#define CONSOLE_PROGRAM "/contest64"
#endif
  char *argv[] = { CONSOLE_PROGRAM, 0 };
  arch_start_user (CONSOLE_PROGRAM, 1, argv);
  for (;;) __asm__ volatile ("wfi");
}
#endif

void kmain (void) {
  u64 el;
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (el));
  printf ("Ulix-arm64 (work in progress)\n");
  printf ("Running at EL%d, sizeof(void*) = %d, kmain at 0x%lx (upper half)\n",
          (int)(el >> 2), (int)sizeof (void *), (u64)kmain);
  irq_init ();
#ifdef CONSOLE_MODE
  console_main ();
#endif
  __asm__ volatile ("svc #0");                    // kernel-mode svc: handler just reports it

  frames_init ();
  user_mm_init ();                                // TTBR0 is now free for user processes
  printf ("MMU self-test:\n");
  mmu_selftest ();

  printf ("disk test:\n");
  disk_test ();

  printf ("book file system test (mx_* from ulix-book.nw):\n");
  extern int bookfs_test (void);
  fails += bookfs_test ();

  printf ("thread and process tests (book scheduler, system calls, fork, exec):\n");
  extern int thread_test (void);
  extern int proc_test (void);
  fails += thread_test ();
  printf ("process test (fork, exec, waitpid, files, faults):\n");
  fails += proc_test ();

  extern void buffer_sync (int);
  buffer_sync (1);                                // write what the tests changed (make verify looks at the image)
  printf (fails ? "RESULT: %d check(s) FAILED\n" : "RESULT: all checks passed\n", fails);
  for (;;) __asm__ volatile ("wfe");
}
