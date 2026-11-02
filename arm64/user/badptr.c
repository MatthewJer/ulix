// passes an unmapped address to a system call: the kernel faults while it reads the user buffer.
// Like on the x86 (page fault handler of the book) this ends in the kernel shell.
#include "ulib.h"

int main (int argc, char **argv) {
  printf ("badptr: write (1, 0x10, 5) with an unmapped buffer\n");
  write (1, (const void *)0x10, 5);
  printf ("badptr: not reached\n");
  return 0;
}
