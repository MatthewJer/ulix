// Self-checking test program: exercises .text, .rodata, .data, .bss and the stack.
// Exit status = number of failed checks.
#include "ulib.h"

static const char greeting[] = "hello from an AArch64 ELF loaded from the Minix disk";   // .rodata
static int table[100] = { 1, 2, 3, 4, 5 };                                                // .data
static char big[9000];                                                                    // .bss, > 2 pages

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

static int depth (int n) { volatile char pad[64]; pad[0] = n; return n == 0 ? 0 : 1 + depth (n - 1) + pad[0] * 0; }

int main (int argc, char **argv) {
  printf ("%s\n", greeting);
  printf ("pid %d, argc=%d\n", getpid (), argc);
  for (int i = 0; i < argc; i++) printf ("argv[%d]=%s\n", i, argv[i]);

  CHECK (argc == 3 && !strcmp (argv[1], "foo") && !strcmp (argv[2], "bar"), "argc/argv delivered on the stack");
  int s = 0; for (int i = 0; i < 100; i++) s += table[i];
  CHECK (s == 15, ".data initialised from the ELF file (sum of table is 15)");
  table[50] = 7; CHECK (table[50] == 7, ".data is writable");
  int z = 0; for (int i = 0; i < (int)sizeof big; i++) z |= big[i];
  CHECK (z == 0, ".bss (9000 bytes, three pages) is zero-filled");
  big[8999] = 'x'; CHECK (big[8999] == 'x', ".bss is writable");
  CHECK (strlen (greeting) == 52, ".rodata string intact");
  CHECK (depth (40) == 40, "recursion uses the user stack");
  yield ();
  return fails;
}
