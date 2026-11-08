// Ulix ARM64 -- the network layers below TCP: Ethernet, ARP, IPv4, ICMP and UDP
//
// A frame that comes from the network card is an Ethernet frame:
//
//     | destination MAC (6) | source MAC (6) | type (2) | data ...
//
// The type says what the data is: 0x0806 = ARP (who has this IP address?), 0x0800 = an IPv4 packet. An IPv4 packet has a header of
// (usually) 20 bytes with the addresses and the protocol of its data: 1 = ICMP (ping), 17 = UDP, 6 = TCP.
//
// Everything is deliberately plain: one frame at a time, no fragmentation, no IP options, a table of eight neighbours. The layers are
// called in a chain, like in the books about TCP/IP: net_poll_once () -> frame_input () -> arp_input () or ip_input () ->
// icmp_input (), udp_input () or tcp_input ().

#include "net.h"

void tcp_input (u32 src_ip, const u8 *segment, int len) __attribute__ ((weak));      // tcp.c (if it is part of the kernel)
void tcp_timer (void) __attribute__ ((weak));

// ---- the addresses of this computer: the ones that qemu's user-mode network expects ----
u32 net_my_ip   = IP (10, 0, 2, 15);
u32 net_netmask = IP (255, 255, 255, 0);
u32 net_gateway = IP (10, 0, 2, 2);          // the router (qemu answers for it: ARP, ping, and it forwards to the outside)
u32 net_dns     = IP (10, 0, 2, 3);          // the name server (not used yet: there is no DNS client)
const char *net_if_name = "vioif0";          // NetBSD calls the virtio network interface vioif0 (FreeBSD: vtnet0)

#define ETH_ARP  0x0806
#define ETH_IP   0x0800
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17

static const u8 broadcast_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static u8 rxframe[1518];                     // the frame that is being handled (handlers may send, so sending has its own buffer)
static u8 txframe[1518];

static void copy (u8 *to, const u8 *from, int n) { for (int i = 0; i < n; i++) to[i] = from[i]; }
static int  same (const u8 *a, const u8 *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }

u64 net_now_ms (void) {                       // the counter of the generic timer runs at a fixed frequency
  u64 freq, count;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (count));
  return count / (freq / 1000);
}

// The checksum of the Internet protocols: the sum of all 16 bit words (a last single byte counts as the high half of a word),
// carries added back in, then all bits turned around. 'sum' lets the caller start with a part that is somewhere else (the
// "pseudo header" of UDP and TCP). A packet that contains its own checksum gives 0 when it is checked.
u16 net_checksum (const u8 *data, int len, u32 sum) {
  for (int i = 0; i + 1 < len; i += 2) sum += get16 (data + i);
  if (len & 1) sum += (u32)data[len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (u16)~sum;
}

// ---------------------------------------------------------------- ARP
// ARP finds out which Ethernet address belongs to an IP address: ask everybody ("who has 10.0.2.2?"), the owner answers.
//
//     | hardware type 1 (2) | protocol 0x0800 (2) | 6 (1) | 4 (1) | operation 1 = request, 2 = reply (2) |
//     | sender MAC (6) | sender IP (4) | target MAC (6) | target IP (4) |

#define ARP_ENTRIES 8
static struct { u32 ip; u8 mac[6]; int valid; } arp_cache[ARP_ENTRIES];
static int arp_next = 0;

static void arp_remember (u32 ip, const u8 *mac) {
  for (int i = 0; i < ARP_ENTRIES; i++)
    if (arp_cache[i].valid && arp_cache[i].ip == ip) { copy (arp_cache[i].mac, mac, 6); return; }
  arp_cache[arp_next].ip = ip;  copy (arp_cache[arp_next].mac, mac, 6);  arp_cache[arp_next].valid = 1;
  arp_next = (arp_next + 1) % ARP_ENTRIES;                       // when the table is full the oldest entry goes
}

static void arp_send (int operation, const u8 *target_mac, u32 target_ip) {
  u8 *f = txframe;
  copy (f, operation == 1 ? broadcast_mac : target_mac, 6);      // a request goes to everybody, a reply to the one who asked
  copy (f + 6, net_dev_mac (), 6);
  put16 (f + 12, ETH_ARP);
  put16 (f + 14, 1);  put16 (f + 16, ETH_IP);  f[18] = 6;  f[19] = 4;  put16 (f + 20, operation);
  copy (f + 22, net_dev_mac (), 6);  put32 (f + 28, net_my_ip);
  copy (f + 32, operation == 1 ? (const u8 *)"\0\0\0\0\0\0" : target_mac, 6);  put32 (f + 38, target_ip);
  net_dev_send (f, 42);
}

static void arp_input (const u8 *f, int len) {
  if (len < 42 || get16 (f + 14) != 1 || get16 (f + 16) != ETH_IP || f[18] != 6 || f[19] != 4) return;
  u32 sender_ip = get32 (f + 28), target_ip = get32 (f + 38);
  int operation = get16 (f + 20);
  if (sender_ip) arp_remember (sender_ip, f + 22);               // whoever speaks to us: we learn how to answer
  if (operation == 1 && target_ip == net_my_ip) arp_send (2, f + 22, sender_ip);
}

static int arp_lookup (u32 ip, u8 *mac) {                         // 0 = it is in the table
  for (int i = 0; i < ARP_ENTRIES; i++)
    if (arp_cache[i].valid && arp_cache[i].ip == ip) { copy (mac, arp_cache[i].mac, 6); return 0; }
  return -1;
}

// the Ethernet address for an IP address on our network: look in the table, else ask (up to four times, waiting a second each
// time for the answer, which arp_input puts into the table); 0 = found
static int arp_resolve (u32 ip, u8 *mac) {
  for (int attempt = 0; attempt < 4; attempt++) {
    if (arp_lookup (ip, mac) == 0) return 0;
    arp_send (1, 0, ip);
    u64 start = net_now_ms ();
    while (net_now_ms () - start < 1000) {
      net_poll_once ();
      if (arp_lookup (ip, mac) == 0) return 0;
    }
  }
  return -1;
}

// ---------------------------------------------------------------- IPv4
//     | version 4 and header length in words (1) | type of service (1) | total length (2) |
//     | identification (2) | flags and fragment offset (2) | time to live (1) | protocol (1) | header checksum (2) |
//     | source IP (4) | destination IP (4) | data ...
// The data of the packet is what follows the header; "total length" says where it ends (Ethernet may pad short frames).

int ip_send (u32 dst, u8 protocol, const u8 *data, int len) {
  static u16 identification = 1;
  if (len > 1480) return -1;                                      // no fragmentation: a packet must fit into one frame
  u8 mac[6];
  if (dst == 0xffffffff) copy (mac, broadcast_mac, 6);
  else {
    u32 next_hop = ((dst ^ net_my_ip) & net_netmask) == 0 ? dst : net_gateway;      // on our network: direct, else via the router
    if (next_hop == 0) return -1;                                 // not on our network and no router: "network is unreachable"
    if (arp_resolve (next_hop, mac) != 0) return -1;
  }
  u8 *f = txframe;
  copy (f, mac, 6);  copy (f + 6, net_dev_mac (), 6);  put16 (f + 12, ETH_IP);
  u8 *ip = f + 14;
  ip[0] = 0x45;  ip[1] = 0;  put16 (ip + 2, 20 + len);
  put16 (ip + 4, identification++);  put16 (ip + 6, 0x4000);     // "don't fragment"
  ip[8] = 64;  ip[9] = protocol;  put16 (ip + 10, 0);
  put32 (ip + 12, net_my_ip);  put32 (ip + 16, dst);
  put16 (ip + 10, net_checksum (ip, 20, 0));
  copy (ip + 20, data, len);
  return net_dev_send (f, 14 + 20 + len);
}

// ---------------------------------------------------------------- ICMP
// ICMP carries messages about the network. We only know the echo request (type 8) and its answer (type 0): "ping".
//     | type (1) | code (1) | checksum (2) | identifier (2) | sequence number (2) | data ...

static volatile int ping_seq_seen = -1;                          // the sequence number of the last echo reply that arrived

// ---- raw ICMP sockets, for programs (see net_syscalls.c): a program builds its own ICMP messages, sends them with icmp_send ()
// and picks up the answers with icmp_get (). That is what the program "ping" does. Everything that arrives and is not an echo
// request (those the kernel answers itself) is put into the queue of every open socket: echo replies (type 0), "destination
// unreachable" (3), "time exceeded" (11), ... The program looks at the type and at the identifier to find its own.
#define ICMP_SOCKETS 4
#define ICMP_QUEUE   8
#define ICMP_MAX     256                                         // longer messages are cut off
static struct {
  int used, owner;
  int first, count;                                              // the queue is a ring: 'count' messages, the oldest at 'first'
  struct { u32 from; int len; u8 data[ICMP_MAX]; } queue[ICMP_QUEUE];
} icmp_sockets[ICMP_SOCKETS];

// A free socket, or one whose owner has gone ('alive' tells whether a process still exists): its number, -1 = none.
int icmp_open (int owner, int (*alive) (int owner)) {
  for (int i = 0; i < ICMP_SOCKETS; i++)
    if (!icmp_sockets[i].used || !alive (icmp_sockets[i].owner)) {
      u8 *p = (u8 *)&icmp_sockets[i];
      for (unsigned k = 0; k < sizeof icmp_sockets[i]; k++) p[k] = 0;
      icmp_sockets[i].used = 1;  icmp_sockets[i].owner = owner;
      return i;
    }
  return -1;
}

void icmp_close (int h) { if (h >= 0 && h < ICMP_SOCKETS) icmp_sockets[h].used = 0; }

int icmp_send (int h, u32 ip, const u8 *msg, int len) {
  if (h < 0 || h >= ICMP_SOCKETS || !icmp_sockets[h].used || len < 8 || len > 1480) return -1;
  return ip_send (ip, IP_ICMP, msg, len);
}

// the oldest waiting message: its length (and the sender in *from), 0 = none, -1 = no such socket
int icmp_get (int h, u8 *buf, int max, u32 *from) {
  if (h < 0 || h >= ICMP_SOCKETS || !icmp_sockets[h].used) return -1;
  if (icmp_sockets[h].count == 0) return 0;
  int i = icmp_sockets[h].first;
  int len = icmp_sockets[h].queue[i].len < max ? icmp_sockets[h].queue[i].len : max;
  for (int k = 0; k < len; k++) buf[k] = icmp_sockets[h].queue[i].data[k];
  *from = icmp_sockets[h].queue[i].from;
  icmp_sockets[h].first = (i + 1) % ICMP_QUEUE;
  icmp_sockets[h].count--;
  return len;
}

static void icmp_deliver (u32 from, const u8 *icmp, int len) {
  for (int h = 0; h < ICMP_SOCKETS; h++) {
    if (!icmp_sockets[h].used || icmp_sockets[h].count == ICMP_QUEUE) continue;     // (a full queue loses the newest message)
    int i = (icmp_sockets[h].first + icmp_sockets[h].count) % ICMP_QUEUE;
    int n = len < ICMP_MAX ? len : ICMP_MAX;
    for (int k = 0; k < n; k++) icmp_sockets[h].queue[i].data[k] = icmp[k];
    icmp_sockets[h].queue[i].len = n;  icmp_sockets[h].queue[i].from = from;
    icmp_sockets[h].count++;
  }
}

static void icmp_input (u32 src, const u8 *icmp, int len) {
  if (len < 8 || net_checksum (icmp, len, 0) != 0) return;
  if (icmp[0] == 8) {                                            // echo request: send the same data back as an echo reply
    static u8 reply[1480];
    if (len > (int)sizeof reply) return;
    copy (reply, icmp, len);
    reply[0] = 0;  put16 (reply + 2, 0);
    put16 (reply + 2, net_checksum (reply, len, 0));
    ip_send (src, IP_ICMP, reply, len);
  } else {
    if (icmp[0] == 0) ping_seq_seen = get16 (icmp + 6);
    icmp_deliver (src, icmp, len);                               // ... and the programs that have a socket may want it, too
  }
}

int net_ping (u32 ip, int timeout_ms) {
  static u16 sequence = 0;
  u8 echo[8 + 32];
  echo[0] = 8;  echo[1] = 0;  put16 (echo + 2, 0);
  put16 (echo + 4, 0x756c);  put16 (echo + 6, ++sequence);       // identifier "ul", our sequence number
  for (int i = 0; i < 32; i++) echo[8 + i] = 'a' + i % 26;
  put16 (echo + 2, net_checksum (echo, sizeof echo, 0));
  ping_seq_seen = -1;
  u64 start = net_now_ms();
  if (ip_send (ip, IP_ICMP, echo, sizeof echo) != 0) return -1;
  while (net_now_ms () - start < (u64)timeout_ms) {
    net_poll_once ();
    if (ping_seq_seen == sequence) return (int)(net_now_ms () - start);
  }
  return -1;
}

// ---------------------------------------------------------------- UDP
// UDP sends single messages ("datagrams") to a port of another computer. No connection, no guarantee that it arrives.
//     | source port (2) | destination port (2) | length of header and data (2) | checksum (2) | data ...
// The checksum also covers a "pseudo header" with the IP addresses and the protocol, so that a datagram that was delivered to
// the wrong address is noticed.

#define UDP_PORTS 8
static struct { u16 port; udp_handler handler; } udp_ports[UDP_PORTS];

int udp_bind (u16 port, udp_handler handler) {
  for (int i = 0; i < UDP_PORTS; i++)
    if (!udp_ports[i].handler) { udp_ports[i].port = port; udp_ports[i].handler = handler; return 0; }
  return -1;
}

// the sum of the pseudo header, to be continued with the checksum of the UDP (or TCP) segment itself
u32 net_pseudo_sum (u32 src, u32 dst, u8 protocol, int length) {
  return (src >> 16) + (src & 0xffff) + (dst >> 16) + (dst & 0xffff) + protocol + length;
}

int udp_send (u32 dst_ip, u16 dst_port, u16 src_port, const u8 *data, int len) {
  static u8 datagram[1480];
  if (len > 1472) return -1;
  put16 (datagram, src_port);  put16 (datagram + 2, dst_port);  put16 (datagram + 4, 8 + len);  put16 (datagram + 6, 0);
  copy (datagram + 8, data, len);
  u16 sum = net_checksum (datagram, 8 + len, net_pseudo_sum (net_my_ip, dst_ip, IP_UDP, 8 + len));
  put16 (datagram + 6, sum ? sum : 0xffff);                       // (0 means "no checksum", so a real 0 is sent as all ones)
  return ip_send (dst_ip, IP_UDP, datagram, 8 + len);
}

static void udp_input (u32 src, u32 dst, const u8 *udp, int len) {
  if (len < 8) return;
  int length = get16 (udp + 4);
  if (length < 8 || length > len) return;
  if (get16 (udp + 6) != 0 && net_checksum (udp, length, net_pseudo_sum (src, dst, IP_UDP, length)) != 0) return;
  for (int i = 0; i < UDP_PORTS; i++)
    if (udp_ports[i].handler && udp_ports[i].port == get16 (udp + 2)) {
      udp_ports[i].handler (src, get16 (udp), udp + 8, length - 8);
      return;
    }
}

// ---------------------------------------------------------------- IPv4, receiving
static void ip_input (const u8 *f, int len) {
  const u8 *ip = f + 14;
  if (len < 14 + 20 || (ip[0] >> 4) != 4) return;
  int header = (ip[0] & 15) * 4, total = get16 (ip + 2);
  if (header < 20 || total < header || 14 + total > len) return;
  if (net_checksum (ip, header, 0) != 0) return;                  // damaged header
  if (get16 (ip + 6) & 0x3fff) return;                            // a fragment (more fragments or an offset): not supported
  u32 src = get32 (ip + 12), dst = get32 (ip + 16);
  if (dst != net_my_ip && dst != 0xffffffff) return;              // not for us
  const u8 *data = ip + header;
  int data_len = total - header;
  switch (ip[9]) {
    case IP_ICMP: icmp_input (src, data, data_len); break;
    case IP_UDP:  udp_input (src, dst, data, data_len); break;
    case IP_TCP:  if (tcp_input) tcp_input (src, data, data_len); break;
  }
}

// ---------------------------------------------------------------- the frame
static void frame_input (const u8 *f, int len) {
  if (len < 14) return;
  const u8 *own = net_dev_mac ();
  if (!same (f, own, 6) && !same (f, broadcast_mac, 6)) return;   // not for this card
  switch (get16 (f + 12)) {
    case ETH_ARP: arp_input (f, len); break;
    case ETH_IP:  ip_input (f, len); break;
  }
}

int net_poll_once (void) {
  if (tcp_timer) tcp_timer ();                                   // (retransmissions are due while we look for frames)
  int len = net_dev_recv (rxframe, sizeof rxframe);
  if (len <= 0) return 0;
  frame_input (rxframe, len);
  return 1;
}

void net_run (int ms) {
  u64 start = net_now_ms();
  while (net_now_ms () - start < (u64)ms) net_poll_once ();
}

int net_init (void) {
  if (net_dev_init () != 0) return -1;
  printf ("net: IP address %u.%u.%u.%u, router %u.%u.%u.%u\n", net_my_ip >> 24, (net_my_ip >> 16) & 255, (net_my_ip >> 8) & 255, net_my_ip & 255,
          net_gateway >> 24, (net_gateway >> 16) & 255, (net_gateway >> 8) & 255, net_gateway & 255);
  return 0;
}
