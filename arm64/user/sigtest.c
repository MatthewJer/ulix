// Tests signals: handlers that interrupt a computation at arbitrary points, SIG_IGN, the default
// action of kill(), and SIGSTOP / SIGCONT.  Exit status = number of failed checks.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

static volatile int got = 0, last_signal = 0;
static void handler (int sig) { got++; last_signal = sig; }

// some arithmetic that uses many registers: any register lost across a signal changes the result
static unsigned __attribute__ ((noinline)) work (int n) {
  unsigned s = 1, a = 3, b = 5, c = 7;
  for (int i = 0; i < n; i++) {
    s = s * 31 + (unsigned)i * 7 + (s >> 3);
    a += s ^ b;  b += a >> 2;  c ^= a + b + s;
    s += c & 0xff;
  }
  return s ^ a ^ b ^ c;
}

#define N 8000000

static int file_size (int fd) { return lseek (fd, 0, 2); }       // SEEK_END

int main (int argc, char **argv) {
  int status = -12345, pid;

  // 1. a handler interrupts a running computation, which still gets the right result
  signal (SIGUSR1, handler);                       // inherited by the child
  pid = fork ();
  if (pid == 0) {
    unsigned r = work (N);
    int n = got > 63 ? 63 : got;                    // number of handler calls in bits 23..28
    exit ((int)((r & 0x7fffff) | ((unsigned)n << 23)));
  }
  for (int i = 0; i < 30; i++) { kill (pid, SIGUSR1); yield (); }
  waitpid (pid, &status, 0);
  unsigned expect = work (N) & 0x7fffff;
  CHECK ((status >> 23) >= 2, "the handler ran several times (before the first instruction and in the middle of the computation)");
  CHECK ((unsigned)(status & 0x7fffff) == expect, "the interrupted computation delivered the exact result");

  // 2. SIG_IGN: the signal is dropped, the process goes on
  signal (SIGUSR2, SIG_IGN);                       // inherited by the child
  pid = fork ();
  if (pid == 0) { work (200000); exit (5); }
  for (int i = 0; i < 5; i++) { kill (pid, SIGUSR2); yield (); }
  waitpid (pid, &status, 0);
  CHECK (status == 5, "an ignored signal does not disturb the child");

  // 3. default action: no handler, so the process is killed
  pid = fork ();
  if (pid == 0) { for (;;) yield (); }
  yield ();
  CHECK (kill (pid, SIGTERM) == 0, "kill (SIGTERM) on a process without handler");
  waitpid (pid, &status, 0);
  CHECK (kill (pid, 0) == -1, "the process is gone afterwards");

  // 4. SIGSTOP / SIGCONT, observed through a file the child appends to
  int log = open ("/progress.txt", 0x202);         // O_CREAT | O_RDWR
  pid = fork ();
  if (pid == 0) {
    int f = open ("/progress.txt", 0x202);
    for (;;) { write (f, "x", 1); yield (); }
  }
  while (file_size (log) < 5) yield ();
  kill (pid, SIGSTOP);
  int s1 = file_size (log);
  for (int i = 0; i < 40; i++) yield ();
  int s2 = file_size (log);
  CHECK (s1 >= 5 && s2 == s1, "a stopped process makes no progress");
  kill (pid, SIGCONT);
  for (int i = 0; i < 40; i++) yield ();
  int s3 = file_size (log);
  CHECK (s3 > s2, "after SIGCONT it goes on");
  kill (pid, SIGKILL);
  CHECK (waitpid (pid, &status, 0) == -1, "SIGKILL removes the process at once (nothing left to wait for)");
  close (log);

  return fails;
}
