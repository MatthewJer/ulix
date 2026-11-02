// Ulix ARM64 -- tiny user-space library (system calls and printf)
#ifndef ULIB_H
#define ULIB_H

typedef unsigned long u64;
typedef unsigned int  u32;
typedef unsigned char u8;

// System call ABI (the same convention as the x86 version of Ulix, with the registers renamed):
// svc #0; x0 = system call number (afterwards: the result), x1..x5 = arguments.
// The numbers are those of the book (__NR_* in ulix-book.nw).
static inline long syscall3 (long nr, long a, long b, long c) {
  register long x0 __asm__ ("x0") = nr;
  register long x1 __asm__ ("x1") = a;
  register long x2 __asm__ ("x2") = b;
  register long x3 __asm__ ("x3") = c;
  __asm__ volatile ("svc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3) : "memory");
  return (int)x0;                          // the kernel returns 32-bit values
}

#define SYS_exit     1
#define SYS_fork     2
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_waitpid  7
#define SYS_execve  11
#define SYS_lseek   19
#define SYS_getpid  20
#define SYS_getppid 64
#define SYS_brk     45
#define SYS_free_frames 509
#define SYS_kill    37
#define SYS_signal  48
#define SYS_resign  66
#define SYS_gettid  21
#define SYS_clrscr      512
#define SYS_get_xy      513
#define SYS_set_xy      514
#define SYS_read_screen 515
#define SYS_write_screen 516
#define SYS_page_out    508
#define SYS_free_a_frame 510
#define SYS_pthread_create 506
#define SYS_pthread_exit   507
#define SYS_mutex_init     517
#define SYS_mutex_lock     518
#define SYS_mutex_unlock   519
#define SYS_mutex_destroy  520
#define SYS_mutex_trylock  526

static inline long write (int fd, const void *buf, u64 n)  { return syscall3 (SYS_write, fd, (long)buf, n); }
static inline int  getpid (void)                           { return syscall3 (SYS_getpid, 0, 0, 0); }
static inline int  getppid (void)                          { return syscall3 (SYS_getppid, 0, 0, 0); }
static inline void yield (void)                            { syscall3 (SYS_resign, 0, 0, 0); }
static inline void exit (int code)                         { syscall3 (SYS_exit, code, 0, 0); for (;;) ; }
static inline int  fork (void)                             { return syscall3 (SYS_fork, 0, 0, 0); }
static inline int  waitpid (int pid, int *status, int opt) { return syscall3 (SYS_waitpid, pid, (long)status, opt); }
static inline int  execv (const char *path, char *const argv[]) { return syscall3 (SYS_execve, (long)path, (long)argv, 0); }
static inline int  open (const char *path, int flags)      { return syscall3 (SYS_open, (long)path, flags, 0); }
static inline long read (int fd, void *buf, u64 n)         { return syscall3 (SYS_read, fd, (long)buf, n); }
typedef void (*sighandler_t) (int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGKILL   9
#define SIGTERM  15
#define SIGSTOP  17
#define SIGCONT  19
#define SIGUSR1  30
#define SIGUSR2  31
static inline sighandler_t signal (int sig, sighandler_t f) { return (sighandler_t)syscall3 (SYS_signal, sig, (long)f, 0); }
static inline int  kill (int pid, int sig)                  { return syscall3 (SYS_kill, pid, sig, 0); }
static inline void *sbrk (int incr)                         { return (void *)(long)syscall3 (SYS_brk, incr, 0, 0); }
static inline int  get_free_frames (void)                   { return syscall3 (SYS_free_frames, 0, 0, 0); }
static inline int  close (int fd)                          { return syscall3 (SYS_close, fd, 0, 0); }
static inline int  lseek (int fd, int off, int whence)     { return syscall3 (SYS_lseek, fd, off, whence); }

static inline int  gettid (void)                            { return syscall3 (SYS_gettid, 0, 0, 0); }
static inline int  pthread_create (void (*f) (void))        { return syscall3 (SYS_pthread_create, (long)f, 0, 0); }
static inline void pthread_exit (void)                      { syscall3 (SYS_pthread_exit, 0, 0, 0); for (;;) ; }
static inline int  mutex_init (int *m)                      { return syscall3 (SYS_mutex_init, (long)m, 0, 0); }
static inline int  mutex_lock (int *m)                      { return syscall3 (SYS_mutex_lock, (long)m, 0, 0); }
static inline int  mutex_unlock (int *m)                    { return syscall3 (SYS_mutex_unlock, (long)m, 0, 0); }
static inline int  mutex_trylock (int *m)                   { return syscall3 (SYS_mutex_trylock, (long)m, 0, 0); }
static inline int  mutex_destroy (int *m)                   { return syscall3 (SYS_mutex_destroy, (long)m, 0, 0); }

static inline int  page_out (void *addr)                    { return syscall3 (SYS_page_out, (long)addr >> 12, 0, 0); }
static inline void free_a_frame (void)                      { syscall3 (SYS_free_a_frame, 0, 0, 0); }

static inline void clrscr (void)                            { syscall3 (SYS_clrscr, 0, 0, 0); }
static inline void get_xy (char *x, char *y)                { syscall3 (SYS_get_xy, (long)x, (long)y, 0); }
static inline void set_xy (char x, char y)                  { syscall3 (SYS_set_xy, x, y, 0); }
static inline void read_screen (char *buf)                  { syscall3 (SYS_read_screen, (long)buf, 0, 0); }
static inline void write_screen (char *buf)                 { syscall3 (SYS_write_screen, (long)buf, 0, 0); }

int  strlen (const char *s);
int  strcmp (const char *a, const char *b);
void printf (const char *fmt, ...);        // %s %c %d %u %x, width / 0-padding

#endif
