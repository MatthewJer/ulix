// cat FILE: reads a file from the Minix disk with open/read/close and writes it to the console
#include "ulib.h"

int main (int argc, char **argv) {
  if (argc < 2) { printf ("usage: cat FILE\n"); return 2; }
  int fd = open (argv[1]);
  if (fd < 0) { printf ("cat: cannot open %s\n", argv[1]); return 1; }
  char buf[200];
  long n, total = 0;
  while ((n = read (fd, buf, sizeof buf)) > 0) { write (1, buf, n); total += n; }
  printf ("\n-- %d bytes\n", (int)total);
  close (fd);
  return 0;
}
