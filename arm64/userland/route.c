#include "../ulixlib.h"
#include "../arm64-extra.h"

// route -- show or change the route to other networks (modelled on route and "netstat -r" of FreeBSD and NetBSD)
//
//     route show                    the routing table (also: route, route -n show)
//     route add default A.B.C.D     send everything that is not on our own network to this router
//     route delete default          remove the router
//
// Ulix has only two routes: the one to our own network (it comes from the address and the netmask) and the default route.
// Only root may change the default route (use su).

static int parse_address (char *s, unsigned *address) {
  unsigned value = 0;
  for (int part = 0; part < 4; part++) {
    if (*s < '0' || *s > '9') return -1;
    unsigned n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    if (n > 255) return -1;
    value = (value << 8) | n;
    if (part < 3) { if (*s != '.') return -1; s++; }
  }
  if (*s) return -1;
  *address = value;
  return 0;
}

static int equal (char *a, char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void show (struct net_info *i) {
  char gateway[24], network[24];
  int bits = 0;
  for (unsigned m = i->netmask; m & 0x80000000; m <<= 1) bits++;
  unsigned net = i->address & i->netmask;
  printf ("Routing tables\n\nInternet:\n");
  printf ("Destination        Gateway            Flags     Netif\n");
  if (i->gateway) {
    sprintf (gateway, "%d.%d.%d.%d", i->gateway >> 24, (i->gateway >> 16) & 255, (i->gateway >> 8) & 255, i->gateway & 255);
    printf ("%-18s %-18s %-9s %s\n", "default", gateway, "UGS", i->name);              // U = up, G = through a gateway, S = set by hand
  }
  if (i->address) {
    sprintf (network, "%d.%d.%d.%d/%d", net >> 24, (net >> 16) & 255, (net >> 8) & 255, net & 255, bits);
    printf ("%-18s %-18s %-9s %s\n", network, "link#1", "U", i->name);                 // the network we are on: directly reachable
  }
}

static void usage (void) { printf ("usage: route [-n] show\n       route add default gateway\n       route delete default\n"); }

int main (int argc, char *argv[]) {
  struct net_info info;
  if (net_info (&info) != 0) { printf ("route: there is no network interface\n"); return 1; }
  int i = 1;
  if (i < argc && equal (argv[i], "-n")) i++;                                  // (addresses are always shown as numbers)
  if (i >= argc || equal (argv[i], "show") || equal (argv[i], "print")) { show (&info); return 0; }

  struct net_set set;
  if (equal (argv[i], "add") && argc == i + 3 && equal (argv[i + 1], "default") && parse_address (argv[i + 2], &set.gateway) == 0) set.what = 2;
  else if (equal (argv[i], "delete") && argc == i + 2 && equal (argv[i + 1], "default")) set.what = 4;
  else { usage (); return 1; }
  int r = net_set (&set);
  if (r == -2) { printf ("route: permission denied (only root may change the routes)\n"); return 1; }
  return r != 0;
}
