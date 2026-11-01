// Console test: reads lines from the console (the UART) with read (0, ...), echoes them, and finally
// spins until the user presses Ctrl-C.
#include "ulib.h"

static int read_line (char *line, int max) {
  int n = 0;
  for (;;) {
    char c = 0;
    read (0, &c, 1);                       // returns 0 when we were woken up without a character
    if (c == 0) continue;
    if (c == '\n') { printf ("\n"); line[n] = 0; return n; }
    if (c == '\b') { if (n > 0) { n--; printf ("\b \b"); } continue; }
    if (n < max - 1) { line[n++] = c; printf ("%c", c); }
  }
}

int main (int argc, char **argv) {
  char line[80];
  printf ("contest: pid %d is reading from the console\n", getpid ());
  for (int round = 1; round <= 2; round++) {
    printf ("prompt%d> ", round);
    read_line (line, sizeof line);
    printf ("you typed: [%s]\n", line);
  }
  printf ("now spinning, press Ctrl-C\n");
  for (;;) yield ();
  return 0;
}
