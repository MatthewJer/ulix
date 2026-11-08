#include "../ulixlib.h"
#include "../arm64-extra.h"

// dhclient -- ask a DHCP server for an address and use it (modelled on dhclient of FreeBSD)
//
//     dhclient [-t seconds] [interface]
//
// The kernel does the talking (see dhcp.c: DISCOVER, OFFER, REQUEST, ACK); this program starts it and tells what came of it.
// Only root may do this (use su).

static int equal (char *a, char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void print_address (unsigned a) { printf ("%d.%d.%d.%d", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255); }

int main (int argc, char *argv[]) {
  struct net_info info;
  if (net_info (&info) != 0) { printf ("dhclient: there is no network interface\n"); return 1; }
  int seconds = 10;
  for (int i = 1; i < argc; i++) {
    if (equal (argv[i], "-t") && i + 1 < argc) { seconds = 0; for (char *s = argv[++i]; *s >= '0' && *s <= '9'; s++) seconds = seconds * 10 + (*s - '0'); }
    else if (!equal (argv[i], info.name)) { printf ("dhclient: interface %s does not exist (usage: dhclient [-t seconds] [interface])\n", argv[i]); return 1; }
  }
  printf ("DHCPDISCOVER on %s to 255.255.255.255 port 67\n", info.name);
  struct net_dhcp_args d;
  d.timeout_ms = seconds * 1000;
  int r = net_dhcp (&d);
  if (r == -2) { printf ("dhclient: permission denied (only root may configure the network)\n"); return 1; }
  if (r != 0) { printf ("No DHCPOFFERS received.\n"); return 1; }
  printf ("DHCPACK from ");  print_address (d.server);  printf ("\n");
  printf ("bound to ");  print_address (d.address);  printf (" netmask ");  print_address (d.netmask);  printf ("\n");
  if (d.gateway) { printf ("router ");  print_address (d.gateway);  printf ("\n"); }
  if (d.dns) { printf ("name server ");  print_address (d.dns);  printf ("\n"); }
  printf ("lease %d seconds\n", d.lease);
  return 0;
}
