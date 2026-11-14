/* semshim.c: named POSIX semaphores for programs that are linked statically with musl (chapter "Python", section on multiprocessing).
 *
 * musl's sem_open keeps a named semaphore in a file of /dev/shm that every process maps into memory; Ulix has no shared memory. Here the kernel
 * keeps the semaphores (system calls 640 to 645 of Ulix, linux_abi.c) and a sem_t that sem_open returns holds only the slot number and a
 * marker. Semaphores that were made with sem_init (the ones threads use, for example inside Python itself) are not touched: the linker is told to
 * send sem_wait, sem_trywait, sem_timedwait, sem_post and sem_getvalue to the __wrap_ functions below (-Wl,--wrap=...), which look for the
 * marker and give every other semaphore to musl's own code (__real_...).
 */
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#define USEM_OPEN 640
#define USEM_WAIT 641
#define USEM_POST 642
#define USEM_VALUE 643
#define USEM_CLOSE 644
#define USEM_UNLINK 645
#define MARK1 0x75736d31   /* "usm1" */
#define MARK2 0x00ca11ed

struct ksem { int mark1, mark2, id; };       /* what a sem_t of the kernel's kind contains (a sem_t has room for 32 bytes) */

static int is_ksem (sem_t *s) { struct ksem *k = (struct ksem *) s;  return k->mark1 == MARK1 && k->mark2 == MARK2; }
/* (musl's syscall () has already turned a negative result into -1 and errno) */
static int result (long r) { return r < 0 ? -1 : 0; }

sem_t *sem_open (const char *name, int flags, ...) {
  unsigned mode = 0, value = 0;
  if (flags & O_CREAT) { va_list ap;  va_start (ap, flags);  mode = va_arg (ap, unsigned);  value = va_arg (ap, unsigned);  va_end (ap); }
  (void) mode;
  while (*name == '/') name++;
  long id = syscall (USEM_OPEN, name, flags, value);
  if (id < 0) return SEM_FAILED;
  struct ksem *k = malloc (sizeof (sem_t));
  if (!k) { syscall (USEM_CLOSE, id);  errno = ENOMEM;  return SEM_FAILED; }
  k->mark1 = MARK1;  k->mark2 = MARK2;  k->id = id;
  return (sem_t *) k;
}

int sem_close (sem_t *s) { long r = syscall (USEM_CLOSE, ((struct ksem *) s)->id);  free (s);  return result (r); }
int sem_unlink (const char *name) { while (*name == '/') name++;  return result (syscall (USEM_UNLINK, name)); }

extern int __real_sem_wait (sem_t *), __real_sem_trywait (sem_t *), __real_sem_timedwait (sem_t *, const struct timespec *), __real_sem_post (sem_t *);
extern int __real_sem_getvalue (sem_t *, int *);

int __wrap_sem_wait (sem_t *s) {
  if (!is_ksem (s)) return __real_sem_wait (s);
  return result (syscall (USEM_WAIT, ((struct ksem *) s)->id, 0, 0));
}
int __wrap_sem_trywait (sem_t *s) { return is_ksem (s) ? result (syscall (USEM_WAIT, ((struct ksem *) s)->id, 1, 0)) : __real_sem_trywait (s); }
int __wrap_sem_post (sem_t *s) { return is_ksem (s) ? result (syscall (USEM_POST, ((struct ksem *) s)->id)) : __real_sem_post (s); }
int __wrap_sem_getvalue (sem_t *s, int *v) {
  if (!is_ksem (s)) return __real_sem_getvalue (s, v);
  long r = syscall (USEM_VALUE, ((struct ksem *) s)->id);
  if (r < 0) return -1;
  *v = r;  return 0;
}
int __wrap_sem_timedwait (sem_t *s, const struct timespec *abs) {   /* abs is a time of CLOCK_REALTIME */
  if (!is_ksem (s)) return __real_sem_timedwait (s, abs);
  struct timespec now;
  clock_gettime (CLOCK_REALTIME, &now);
  long long ns = (abs->tv_sec - now.tv_sec) * 1000000000LL + (abs->tv_nsec - now.tv_nsec);
  if (ns < 0) ns = 0;
  return result (syscall (USEM_WAIT, ((struct ksem *) s)->id, 2, ns));
}
