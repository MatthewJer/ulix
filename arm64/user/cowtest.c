// Copy-on-write fork: the child shares the pages of the parent until somebody writes.
// The number of free frames of the system tells how many pages were really copied.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

#define PAGES 64

int main (int argc, char **argv) {
  char *buf = sbrk (PAGES * 4096);
  CHECK ((long)buf > 0, "sbrk gives 64 pages (256 KByte)");
  for (int i = 0; i < PAGES; i++) { buf[i * 4096] = i + 1; buf[i * 4096 + 4095] = 'z'; }

  int before = get_free_frames ();
  int pid = fork ();
  if (pid == 0) {
    int after_fork = get_free_frames ();
    CHECK (before - after_fork < 16, "fork copied no data pages (only page tables and the kernel stack)");

    int sum = 0;
    for (int i = 0; i < PAGES; i++) sum += buf[i * 4096];
    int after_read = get_free_frames ();
    CHECK (sum == PAGES * (PAGES + 1) / 2, "the child sees the data of the parent");
    CHECK (after_fork == after_read, "reading does not copy anything");

    for (int i = 0; i < PAGES; i++) buf[i * 4096] = 100 + i;
    int after_write = get_free_frames ();
    CHECK (after_read - after_write == PAGES, "writing one byte per page copies exactly the 64 pages");

    int ok = 1;
    for (int i = 0; i < PAGES; i++) if (buf[i * 4096] != 100 + i || buf[i * 4096 + 4095] != 'z') ok = 0;
    CHECK (ok, "the copies hold the new and the untouched old bytes");
    exit (fails);
  }

  int status = -1;
  waitpid (pid, &status, 0);
  CHECK (status == 0, "the child's checks all passed");
  int ok = 1;
  for (int i = 0; i < PAGES; i++) if (buf[i * 4096] != i + 1 || buf[i * 4096 + 4095] != 'z') ok = 0;
  CHECK (ok, "the parent still has its own data (the child's writes went to copies)");

  // the parent writes to pages that are no longer shared: no copy needed any more
  int f1 = get_free_frames ();
  for (int i = 0; i < PAGES; i++) buf[i * 4096] = 7;
  CHECK (f1 == get_free_frames (), "after the child is gone the parent writes without copying");
  CHECK (get_free_frames () >= before - 8, "all frames of the child were returned (the kernel stack is freed later)");
  return fails;
}
