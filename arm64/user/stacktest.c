// The user mode stack grows when the program touches the page just below it, but not further down.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

static int deep (int n) {                  // about 1 KByte of stack per level
  volatile char pad[1000];
  pad[0] = (char)n;  pad[999] = (char)(n + 1);
  if (n == 0) return 0;
  int r = deep (n - 1);
  return r + (pad[0] == (char)n && pad[999] == (char)(n + 1) ? 1 : 1000000);
}

int main (int argc, char **argv) {
  CHECK (deep (600) == 600, "recursion 600 levels deep (about 600 KByte of stack, the start is 16 KByte)");

  int before = get_free_frames ();
  deep (300);
  CHECK (get_free_frames () - before == 0, "the grown stack stays: calling again needs no new frames");

  int pid = fork ();
  if (pid == 0) {                          // touching memory far below the stack ends the process
    volatile char *p = (volatile char *)&pid - 2 * 1024 * 1024;
    *p = 1;
    printf ("  user FAIL: access 2 MByte below the stack was allowed\n");
    exit (0);
  }
  int status = 0;
  waitpid (pid, &status, 0);
  CHECK (status == -1, "an access far below the stack is a fault: the process is killed");

  pid = fork ();
  if (pid == 0) {                          // the child has the grown stack, too
    exit (deep (500) == 500 ? 0 : 1);
  }
  waitpid (pid, &status, 0);
  CHECK (status == 0, "a forked child can use the grown stack");
  exit (fails);
}
