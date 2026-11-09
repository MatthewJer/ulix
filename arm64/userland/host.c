#include "../ulixlib.h"
#include "../arm64-extra.h"


int main (int argc, char *argv[]) {
  if (argc != 2) { printf ("usage: host name\n"); return 1; }
  unsigned a;
  int r = resolve_host (argv[1], &a);
  if (r == 0) { printf ("%s has address %d.%d.%d.%d\n", argv[1], a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);  return 0; }
  if (r == -2) printf ("Host %s not found: 3(NXDOMAIN)\n", argv[1]);
  else if (r == -1) printf (";; connection timed out; no servers could be reached\n");
  else printf ("host: cannot look up %s (is there a name server? see ifconfig)\n", argv[1]);
  return 1;
}

