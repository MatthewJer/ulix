// /bin/swapper for AArch64: this port has no swap space, so there is nothing to swap. Like the real
// swapper it moves to console 10 (setterm (9)) and stays alive: /bin/login does not wait for it, and
// a process that ended would stay a zombie on the visible console, where Ctrl-C would hit it.
#include "../ulixlib.h"
int main (int argc, char **argv) {
  setterm (9);
  setpsname ("swapper");
  for (;;) idle ();
}
