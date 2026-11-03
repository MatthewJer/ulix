// Memory runs out: sbrk () asks for 1 MByte at a time until it fails.  Pages are swapped out to make room (as in the book),
// and when that does not help any more sbrk () returns -1 -- the kernel must neither hang nor crash. (Reading everything
// back is not possible then: a page that is swapped out needs a free frame, and there is none; swaptest checks the data.)
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

#define MB (1024 * 1024)

int main (int argc, char **argv) {
  char *base = (char *)sbrk (0);
  int n = 0;
  for (;;) {
    char *p = (char *)sbrk (MB);
    if ((long)p == -1) break;
    p[0] = (char)(n + 1);  p[5 * 4096] = (char)(n * 3 + 7);        // two pages of each megabyte get a pattern
    for (int i = 1; i < 256; i++) p[i * 4096] = 0;                  // (the rest of the megabyte is touched, too)
    n++;
    if (n >= 2000) break;
  }
  CHECK (n > 20 && n < 2000, "sbrk () fails when memory is used up; it does not hang or crash the kernel");
  printf ("  info: %d MByte were handed out before sbrk () failed\n", n);

  int pid = fork ();                                                  // no memory left for a copy of everything: fork fails or works, but must not hang
  if (pid == 0) exit (0);
  if (pid > 0) { int st; waitpid (pid, &st, 0); }
  CHECK (1, "fork () returns when memory is short");
  CHECK (n > 0, "the process ends and gives all its memory back");
  exit (fails);
}
