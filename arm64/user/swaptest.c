// Swapping: pages are written to /tmp/swap and come back on the next access -- by the program itself, by the
// kernel inside a system call, and in a forked child.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

#define PAGES 48
static char *buf;

static void fill (int p) { for (int i = 0; i < 4096; i++) buf[p * 4096 + i] = (char)(p * 7 + i); }
static int  intact (int p) { for (int i = 0; i < 4096; i++) if (buf[p * 4096 + i] != (char)(p * 7 + i)) return 0; return 1; }
static int  all_intact (void) { for (int p = 0; p < PAGES; p++) if (!intact (p)) return 0; return 1; }

int main (int argc, char **argv) {
  buf = (char *)(((long)sbrk (0) + 4095) & ~4095L);
  CHECK (sbrk ((PAGES + 1) * 4096) != (void *)-1, "sbrk gives 48 pages");
  for (int p = 0; p < PAGES; p++) fill (p);

  CHECK (page_out (0) == -1, "page_out of an unmapped page fails");

  int free0 = get_free_frames ();
  int ok = 1;
  for (int p = 0; p < 16; p++) if (page_out (buf + p * 4096) != 0) ok = 0;
  CHECK (ok, "page_out of 16 pages");
  int free1 = get_free_frames ();
  CHECK (free1 - free0 == 16, "the 16 frames are free again");
  CHECK (page_out (buf) == -1, "a page that is already paged out cannot be paged out again");
  CHECK (all_intact (), "all 48 pages hold their data (the 16 paged-out pages came back on access)");
  int free2 = get_free_frames ();
  CHECK (free2 == free0, "paging in took the frames again");

  // the kernel uses a user pointer: read () into a paged-out page, write () from one
  page_out (buf + 20 * 4096);
  int fd = open ("/hello.c", 0);
  long n = read (fd, buf + 20 * 4096, 100);
  close (fd);
  CHECK (n == 100 && buf[20 * 4096] == '#' && buf[20 * 4096 + 1] == 'i', "read () into a paged-out page: the kernel paged it in");
  fill (20);
  page_out (buf + 21 * 4096);
  buf[21 * 4096] = 0;
  const char *msg = "  user ok:   write () from a paged-out page\n";
  for (int i = 0; msg[i]; i++) buf[21 * 4096 + i] = msg[i];
  page_out (buf + 21 * 4096);
  write (1, buf + 21 * 4096, strlen (msg));
  fill (21);

  // fork with paged-out pages: the child gets its own copy
  for (int p = 24; p < 32; p++) page_out (buf + p * 4096);
  int pid = fork ();
  if (pid == 0) {
    CHECK (all_intact (), "child: all pages intact (the paged-out ones were read back for fork)");
    for (int p = 0; p < PAGES; p++) buf[p * 4096] = 99;
    exit (fails);
  }
  int status = -1;
  waitpid (pid, &status, 0);
  CHECK (status == 0, "the child's checks passed");
  CHECK (all_intact (), "parent: its pages are unchanged by the child");

  // page replacement: free_a_frame pages out the page with the lowest access counter
  int before = get_free_frames ();
  for (int i = 0; i < 10; i++) free_a_frame ();
  int after = get_free_frames ();
  CHECK (after - before == 10, "free_a_frame x 10 freed 10 frames");
  CHECK (all_intact (), "all pages intact after page replacement");

  exit (fails);
}
