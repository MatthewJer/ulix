#include "../ulixlib.h"
#include "../arm64-extra.h"

// ifconfig -- show or set the address of the network interface (modelled on ifconfig of FreeBSD and NetBSD)
//
//     ifconfig [interface]                                     show
//     ifconfig interface [inet] A.B.C.D [netmask MASK]         set the address; MASK as 255.255.255.0 or 0xffffff00
//     ifconfig interface [inet] A.B.C.D/N                      set the address; N = number of bits of the netmask
//
// Only root may change the address (use su).

static int parse_address (char *s, unsigned *address, char **end) {                // "10.0.2.15" -> number; 0 = ok
  unsigned value = 0;
  for (int part = 0; part < 4; part++) {
    if (*s < '0' || *s > '9') return -1;
    unsigned n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    if (n > 255) return -1;
    value = (value << 8) | n;
    if (part < 3) { if (*s != '.') return -1; s++; }
  }
  *address = value;
  *end = s;
  return 0;
}

static int hex_digit (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// a netmask: dotted (255.255.255.0) or hexadecimal (0xffffff00), the way the BSDs print and accept it
static int parse_netmask (char *s, unsigned *mask) {
  char *end;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    unsigned v = 0;
    s += 2;
    if (hex_digit (*s) < 0) return -1;
    for (; hex_digit (*s) >= 0; s++) v = v * 16 + hex_digit (*s);
    if (*s) return -1;
    *mask = v;
    return 0;
  }
  if (parse_address (s, mask, &end) != 0 || *end) return -1;
  return 0;
}

static void show (struct net_info *i) {
  printf ("%s: flags=%x<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu %d\n", i->name, 0x8843, i->mtu);
  printf ("\tether %02x:%02x:%02x:%02x:%02x:%02x\n", i->mac[0], i->mac[1], i->mac[2], i->mac[3], i->mac[4], i->mac[5]);
  unsigned broadcast = i->address | ~i->netmask;
  if (i->address)
    printf ("\tinet %d.%d.%d.%d netmask 0x%x broadcast %d.%d.%d.%d\n", i->address >> 24, (i->address >> 16) & 255, (i->address >> 8) & 255, i->address & 255,
            i->netmask, broadcast >> 24, (broadcast >> 16) & 255, (broadcast >> 8) & 255, broadcast & 255);
}

static void usage (void) {
  printf ("usage: ifconfig [interface]\n       ifconfig interface [inet] address [netmask mask]\n       ifconfig interface [inet] address/bits\n");
}

int main (int argc, char *argv[]) {
  struct net_info info;
  if (net_info (&info) != 0) { printf ("ifconfig: there is no network interface\n"); return 1; }
  if (argc == 1) { show (&info); return 0; }

  // the interface must be the one we have
  char *n = argv[1], *m = info.name;
  while (*n && *n == *m) { n++; m++; }
  if (*n || *m) { printf ("ifconfig: interface %s does not exist\n", argv[1]); return 1; }
  if (argc == 2) { show (&info); return 0; }

  int i = 2;
  if (argv[i][0] == 'i' && argv[i][1] == 'n' && argv[i][2] == 'e' && argv[i][3] == 't' && !argv[i][4]) i++;      // the word "inet" may be left out
  if (i >= argc) { usage (); return 1; }
  struct net_set set;
  char *end;
  if (parse_address (argv[i], &set.address, &end) != 0) { usage (); return 1; }
  set.netmask = info.netmask;
  if (*end == '/') {                                                      // 10.0.2.15/24
    int bits = 0;
    for (end++; *end >= '0' && *end <= '9'; end++) bits = bits * 10 + (*end - '0');
    if (*end || bits > 32) { usage (); return 1; }
    set.netmask = bits == 0 ? 0 : ~((1u << (32 - bits)) - 1);
    if (bits == 32) set.netmask = 0xffffffff;
  } else if (*end) { usage (); return 1; }
  i++;
  if (i < argc) {
    if (!(argv[i][0] == 'n' && argv[i][1] == 'e' && argv[i][2] == 't' && argv[i][3] == 'm' && argv[i][4] == 'a' && argv[i][5] == 's' && argv[i][6] == 'k' && !argv[i][7]) ||
        i + 1 >= argc || parse_netmask (argv[i + 1], &set.netmask) != 0 || i + 2 < argc) { usage (); return 1; }
  }
  set.what = 1;
  int r = net_set (&set);
  if (r == -2) { printf ("ifconfig: permission denied (only root may change the address)\n"); return 1; }
  return r != 0;
}
