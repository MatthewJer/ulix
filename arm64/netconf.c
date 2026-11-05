// Ulix ARM64 -- the configuration of the network at boot: from the kernel command line
//
// The command line (the Raspberry Pi: the file cmdline.txt; qemu: -append) can say how the network is set up. The words are
// separated by blanks; what is not mentioned stays as net.c has it (the address that qemu's user network expects):
//
//     ip=dhcp                  ask a DHCP server for the address, the netmask, the router and the name server
//     ip=10.0.0.5/24           a fixed address, the number after the slash is the length of the netmask in bits
//     gw=10.0.0.1              the router (the "default gateway")
//     dns=10.0.0.1             the name server
//     netecho                  switch on the echo service on port 7 (a test: UDP and TCP send back what they get)
//
// Later the programs ifconfig, route and dhclient can change all this while the system runs (like on the BSDs).

#include "net.h"

int net_echo = 0;                       // "netecho" on the command line: the echo service is on

int net_parse_ip (const char *s, u32 *ip, const char **end) {
  u32 value = 0;
  for (int part = 0; part < 4; part++) {
    if (*s < '0' || *s > '9') return -1;
    u32 n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    if (n > 255) return -1;
    value = (value << 8) | n;
    if (part < 3) { if (*s != '.') return -1; s++; }
  }
  *ip = value;
  if (end) *end = s;
  return 0;
}

static void print_ip (u32 ip) { printf ("%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }

void net_configure_from_command_line (void) {
  const char *p = plat_cmdline ();
  int use_dhcp = 0, set_by_hand = 0;
  while (*p) {
    while (*p == ' ') p++;
    const char *word = p;
    while (*p && *p != ' ') p++;
    if (p - word > 3 && word[0] == 'i' && word[1] == 'p' && word[2] == '=') {
      if (word[3] == 'd' && word[4] == 'h' && word[5] == 'c' && word[6] == 'p') use_dhcp = 1;
      else {
        const char *end;
        u32 ip;
        if (net_parse_ip (word + 3, &ip, &end) == 0) {
          net_my_ip = ip;  set_by_hand = 1;
          if (*end == '/') {                                                     // the length of the netmask in bits
            int bits = 0;
            for (end++; *end >= '0' && *end <= '9'; end++) bits = bits * 10 + (*end - '0');
            net_netmask = bits >= 32 ? 0xffffffff : bits <= 0 ? 0 : ~((1u << (32 - bits)) - 1);
          }
        }
      }
    } else if (p - word == 7 && word[0] == 'n' && word[1] == 'e' && word[2] == 't' && word[3] == 'e' && word[4] == 'c' && word[5] == 'h' && word[6] == 'o') {
      net_echo = 1;
    } else if (p - word > 3 && word[0] == 'g' && word[1] == 'w' && word[2] == '=') {
      net_parse_ip (word + 3, &net_gateway, 0);  set_by_hand = 1;
    } else if (p - word > 4 && word[0] == 'd' && word[1] == 'n' && word[2] == 's' && word[3] == '=') {
      net_parse_ip (word + 4, &net_dns, 0);  set_by_hand = 1;
    }
  }
  if (set_by_hand) {
    printf ("net: address ");  print_ip (net_my_ip);  printf (" netmask ");  print_ip (net_netmask);
    printf (" router ");  print_ip (net_gateway);  printf (" name server ");  print_ip (net_dns);  printf (" (from the command line)\n");
  }
  if (use_dhcp) {
    printf ("net: asking a DHCP server for an address ...\n");
    if (dhcp_run (8000, 0) == 0) { printf ("net: DHCP: address ");  print_ip (net_my_ip);  printf (", router ");  print_ip (net_gateway);  printf ("\n"); }
    else printf ("net: no answer from a DHCP server, keeping the address that was set\n");
  }
}
