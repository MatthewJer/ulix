// Ulix ARM64 -- tiny user-space library (system calls and printf)
#ifndef ULIB_H
#define ULIB_H

typedef unsigned long u64;
typedef unsigned int  u32;
typedef unsigned char u8;

static inline long syscall3 (long nr, long a, long b, long c) {
  register long x8 __asm__ ("x8") = nr;
  register long x0 __asm__ ("x0") = a;
  register long x1 __asm__ ("x1") = b;
  register long x2 __asm__ ("x2") = c;
  __asm__ volatile ("svc #0" : "+r" (x0) : "r" (x8), "r" (x1), "r" (x2) : "memory");
  return x0;
}

#define SYS_write   1
#define SYS_getpid  2
#define SYS_yield   3
#define SYS_exit    4
#define SYS_open    5
#define SYS_read    6
#define SYS_close   7

static inline long write (int fd, const void *buf, u64 n) { return syscall3 (SYS_write, fd, (long)buf, n); }
static inline int  getpid (void)                           { return syscall3 (SYS_getpid, 0, 0, 0); }
static inline void yield (void)                            { syscall3 (SYS_yield, 0, 0, 0); }
static inline void exit (int code)                         { syscall3 (SYS_exit, code, 0, 0); for (;;) ; }
static inline int  open (const char *path)                 { return syscall3 (SYS_open, (long)path, 0, 0); }
static inline long read (int fd, void *buf, u64 n)         { return syscall3 (SYS_read, fd, (long)buf, n); }
static inline int  close (int fd)                          { return syscall3 (SYS_close, fd, 0, 0); }

int  strlen (const char *s);
int  strcmp (const char *a, const char *b);
void printf (const char *fmt, ...);        // %s %c %d %u %x, width / 0-padding

#endif
