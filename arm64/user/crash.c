// writes to its own code page: the kernel must kill this process (permission fault)
#include "ulib.h"

int main (int argc, char **argv) {
  printf ("crash: about to write to my own .text\n");
  *(volatile int *)main = 0;
  printf ("crash: not reached\n");
  return 0;
}
