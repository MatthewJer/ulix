// The screen functions of the book: clrscr, set_xy, get_xy, read_screen, write_screen, wrapping and scrolling.
// The checks are made first and printed at the end; the program leaves "DONE" at column 2 of line 3.
#include "ulib.h"

typedef unsigned short word;
#define W 80
#define H 24
static word scr[W * H];

static int results[16], names_n = 0;
static const char *names[16];
static void check (int ok, const char *name) { results[names_n] = ok; names[names_n++] = name; }
static int cell_is (int x, int y, char c) { return (scr[y * W + x] & 0xff) == (unsigned char)c; }

int main (int argc, char **argv) {
  char x, y;

  clrscr ();
  get_xy (&x, &y);
  check (x == 0 && y == 0, "clrscr puts the cursor in the top left corner");

  set_xy (10, 5);
  printf ("HELLO");
  get_xy (&x, &y);
  check (x == 15 && y == 5, "set_xy, printf and get_xy: the cursor is behind the text");
  read_screen ((char *)scr);
  check (cell_is (10, 5, 'H') && cell_is (14, 5, 'O') && cell_is (9, 5, ' ') && cell_is (15, 5, ' '), "read_screen sees the text at the cursor position");
  check ((scr[5 * W + 10] >> 8) == 0x0F, "normal text has the attribute 0x0F");

  clrscr ();
  for (int i = 0; i < W + 5; i++) printf ("a");
  get_xy (&x, &y);
  read_screen ((char *)scr);
  check (x == 5 && y == 1, "after 85 characters the cursor is at column 5 of line 1 (wrapped)");
  check (cell_is (79, 0, 'a') && cell_is (4, 1, 'a') && cell_is (5, 1, ' '), "the text wrapped to the next line");

  clrscr ();
  set_xy (0, 23);
  printf ("X\nY");
  get_xy (&x, &y);
  read_screen ((char *)scr);
  check (x == 1 && y == 23, "a newline in the last line scrolls: the cursor stays in line 23");
  check (cell_is (0, 22, 'X') && cell_is (0, 23, 'Y'), "the screen scrolled by one line");

  for (int i = 0; i < W * H; i++) scr[i] = ' ' | 0x0F00;
  const char *msg = "WRITTEN";
  for (int i = 0; msg[i]; i++) scr[3 * W + 20 + i] = msg[i] | 0x1F00;   // white on blue
  write_screen ((char *)scr);
  for (int i = 0; i < W * H; i++) scr[i] = 0;
  read_screen ((char *)scr);
  check (cell_is (20, 3, 'W') && cell_is (26, 3, 'N') && (scr[3 * W + 20] >> 8) == 0x1F, "write_screen and read_screen: text and color attribute come back");

  clrscr ();
  int bad = 0;
  for (int i = 0; i < names_n; i++) if (!results[i]) bad++;
  for (int i = 0; i < names_n; i++)
    printf ("  %s %s\n", results[i] ? "user ok:  " : "user FAIL:", names[i]);
  clrscr ();
  set_xy (2, 3);
  printf ("DONE");
  exit (bad);
}
