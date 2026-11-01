// Ulix ARM64 -- kernel entry, PL011 console, minimal printf

#include "ulix.h"


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

static void puts_ (const char *s) { while (*s) putch (*s++); }

static void putnum (u64 v, int base, int width, char pad) {
  char buf[32]; int n = 0;
  do { buf[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
  while (width > n) { putch (pad); width--; }
  while (n) putch (buf[--n]);
}

// supports %s %c %d %u %x %lx %% with optional 0-padding / width
int printf (const char *fmt, ...) {
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
  return 0;
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
  static u8 buf[32768] __attribute__ ((aligned (16)));
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
  CHECK (n == 21, "root directory has 21 entries (16 from x86 Ulix + 5 AArch64 files)");
  n = 0;
  minix_readdir (minix_lookup ("/bin"), count_cb, &n);
  CHECK (n == 63, "/bin has 63 entries");
  CHECK (minix_lookup ("/nonexistent") == 0, "unknown path not found");

  // reference values computed on the host from the same image with an independent parser
  u32 size, sum;
  sum = file_sum ("/hello.c", &size);
  CHECK (size == 316 && sum == 22888, "/hello.c: 316 bytes, checksum matches");
  sum = file_sum ("/test.txt", &size);
  CHECK (size == 1680 && sum == 82026, "/test.txt: two zones, checksum matches");
  sum = file_sum ("/init", &size);
  CHECK (size == 8192 && sum == 592953, "/init: 8 KByte (uses the indirect zone), checksum matches");
  sum = file_sum ("/bin/vi", &size);
  CHECK (size == 25348 && sum == 1364660, "/bin/vi: 25 KByte through a subdirectory, checksum matches");

  u8 text[64];
  long got = minix_read (minix_lookup ("/hello.c"), 0, text, 40);
  text[got > 0 ? got : 0] = 0;
  printf ("  info: /hello.c starts with: %s\n", text);
}

static int count_in (const char *hay, const char *needle) {
  int n = 0, nl = 0;
  while (needle[nl]) nl++;
  for (int i = 0; hay[i]; i++) {
    int j = 0;
    while (j < nl && hay[i + j] == needle[j]) j++;
    if (j == nl) n++;
  }
  return n;
}

// start a program from the disk, wait until everything has exited, return its pid
static int run_elf (const char *path, int argc, const char *const *argv) {
  int pid = task_create_elf (path, argc, argv);
  if (pid < 0) return pid;
  while (tasks_live ()) {
    tasks_reap ();
    if (tasks_live ()) __asm__ volatile ("wfi");
  }
  tasks_reap ();
  return pid;
}

static void elf_test (void) {
  long frames_before = frames_in_use;
  char *out;
  int pid;

  const char *hello_argv[] = { "hello64", "foo", "bar" };
  capture_start ();
  pid = run_elf ("/hello64", 3, hello_argv);
  capture_stop (&out);
  CHECK (pid > 0 && task_exit_code (pid) == 0, "/hello64 ran and reported 0 failed checks (exit status 0)");
  CHECK (count_in (out, "hello from an AArch64 ELF") == 1 && count_in (out, "argv[2]=bar") == 1,
         "program output reached the console, argv visible");
  CHECK (count_in (out, "user ok:") == 7 && count_in (out, "user FAIL") == 0,
         "7 checks inside the program passed: .data, .bss, .rodata, stack, argv");

  const char *cat_argv[] = { "cat64", "/hello.c" };
  capture_start ();
  pid = run_elf ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (pid > 0 && task_exit_code (pid) == 0 && count_in (out, "-- 316 bytes") == 1 &&
         count_in (out, "#include \"../ulixlib.h\"") == 1,
         "/cat64 /hello.c: open/read/close syscalls deliver the 316-byte file");

  cat_argv[1] = "/test.txt";
  capture_start ();
  pid = run_elf ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (task_exit_code (pid) == 0 && count_in (out, "-- 1680 bytes") == 1,
         "/cat64 /test.txt: file spanning two zones, read in several chunks");

  cat_argv[1] = "/nonexistent";
  capture_start ();
  pid = run_elf ("/cat64", 2, cat_argv);
  capture_stop (&out);
  CHECK (task_exit_code (pid) == 1 && count_in (out, "cannot open") == 1, "open of a missing file fails cleanly");

  const char *crash_argv[] = { "crash64" };
  int killed = procs_killed;
  capture_start ();
  pid = run_elf ("/crash64", 1, crash_argv);
  capture_stop (&out);
  CHECK (procs_killed == killed + 1 && task_exit_code (pid) == -1 && count_in (out, "not reached") == 0,
         "write to the read-only code page kills the process (W^X enforced)");

  capture_start ();
  CHECK (task_create_elf ("/badarch64", 1, crash_argv) == -1, "ELF with e_machine = x86 is refused");
  capture_stop (&out);
  CHECK (count_in (out, "not an AArch64 executable") == 1, "  ... with the right reason");
  capture_start ();
  CHECK (task_create_elf ("/badwx64", 1, crash_argv) == -1, "ELF with a writable+executable segment is refused");
  capture_stop (&out);
  CHECK (count_in (out, "writable and executable") == 1, "  ... with the right reason");
  CHECK (task_create_elf ("/init", 1, crash_argv) == -1, "flat x86 binary /init is refused (no ELF header)");
  CHECK (task_create_elf ("/hello.c", 1, crash_argv) == -1, "text file is refused (no ELF header)");
  CHECK (task_create_elf ("/nonexistent", 1, crash_argv) == -1, "missing file is refused");
  CHECK (tasks_live () == 0 && frames_in_use == frames_before,
         "no frame leaked after 6 runs and 5 refused loads");
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

  printf ("disk test:\n");
  disk_test ();

  sched_init ();
  printf ("book file system test (mx_* from ulix-book.nw):\n");
  extern int bookfs_test (void);
  fails += bookfs_test ();

  printf ("process test:\n");
  process_test ();
  printf ("ELF test:\n");
  elf_test ();

  printf ("thread test (scheduler, queues, mutexes, semaphores from ulix-book.nw):\n");
  extern int thread_test (void);
  fails += thread_test ();

  printf (fails ? "RESULT: %d check(s) FAILED\n" : "RESULT: all checks passed\n", fails);
  for (;;) __asm__ volatile ("wfe");
}
