// Tests fork, exec and waitpid (system calls of the book's kernel).  Exit status = failed checks.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

static int shared = 5;                      // .data: every process has its own copy

int main (int argc, char **argv) {
  int status = 12345, pid;
  printf ("forktest: pid %d, parent %d\n", getpid (), getppid ());

  // 1. fork, exit status, separate address spaces
  pid = fork ();
  if (pid == 0) {
    shared = 99;
    printf ("child: pid %d, parent %d, shared = %d\n", getpid (), getppid (), shared);
    exit (7);
  }
  CHECK (pid > 0, "fork returns the child's pid in the parent");
  CHECK (waitpid (pid, &status, 0) == pid, "waitpid returns the pid of the child");
  CHECK (status == 7, "exit status 7 of the child arrives");
  CHECK (shared == 5, "a write in the child did not change the parent (separate address spaces)");

  // 2. fork + exec
  pid = fork ();
  if (pid == 0) {
    char *args[] = { "hello64", "foo", "bar", 0 };
    execv ("/hello64", args);
    printf ("exec failed\n");
    exit (99);
  }
  waitpid (pid, &status, 0);
  CHECK (status == 0, "child exec'ed /hello64, which reports 0 failed checks");

  // 3. several children at the same time, they take turns
  int pids[3];
  for (int i = 0; i < 3; i++) {
    pids[i] = fork ();
    if (pids[i] == 0) {
      for (int k = 0; k < 3; k++) { printf ("worker %d round %d\n", i, k); yield (); }
      exit (10 + i);
    }
  }
  int ok = 1;
  for (int i = 0; i < 3; i++) {
    waitpid (pids[i], &status, 0);
    if (status != 10 + i) ok = 0;
  }
  CHECK (ok, "three children ran concurrently, each exit status arrived");

  // 4. a child that crashes is killed, the parent goes on
  pid = fork ();
  if (pid == 0) {
    char *args[] = { "crash64", 0 };
    execv ("/crash64", args);
    exit (99);
  }
  waitpid (pid, &status, 0);
  CHECK (status == -1, "the crashing child was killed by the kernel (exit status -1)");

  // 5. exec of a missing file fails, the process keeps running
  pid = fork ();
  if (pid == 0) {
    char *args[] = { "x", 0 };
    int r = execv ("/nonexistent", args);
    exit (r == -1 ? 42 : 43);
  }
  waitpid (pid, &status, 0);
  CHECK (status == 42, "execv of a missing file returns -1");

  // 6. grandchild
  pid = fork ();
  if (pid == 0) {
    int g = fork ();
    if (g == 0) exit (3);
    int st = 0;
    waitpid (g, &st, 0);
    exit (st + 20);
  }
  waitpid (pid, &status, 0);
  CHECK (status == 23, "grandchild: exit status 3 passed through the child (3 + 20)");

  return fails;
}
