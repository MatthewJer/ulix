// Ulix ARM64 -- a DHCP client: asks a server on the network for an IP address
//
// A computer that has just been plugged into a network does not know its address. It asks everybody (broadcast), and a DHCP server
// (usually the router) answers. Four messages, all of them UDP datagrams (the server listens on port 67, the client on port 68):
//
//     client -> everybody   DISCOVER   "is there a DHCP server?"
//     server -> client      OFFER      "you could have the address 10.0.2.15, netmask ..., router ..., for 86400 seconds"
//     client -> everybody   REQUEST    "I take the address 10.0.2.15 from the server 10.0.2.2"   (everybody: other servers may have offered, too)
//     server -> client      ACK        "it is yours"           (or NAK: "no")
//
// The messages have the format of BOOTP (RFC 951) with a list of "options" at the end (RFC 2131, 2132):
//
//     | op (1) 1 = request, 2 = reply | hardware type 1 | address length 6 | hops | transaction id (4) | seconds (2) | flags (2) |
//     | client IP (4) | "your" IP (4) | server IP (4) | relay IP (4) | client hardware address (16) | server name (64) | file (128) |
//     | magic cookie 0x63825363 (4) | options: code (1), length (1), data ... | 255 |
//
// While the client has no address its IP address is 0.0.0.0, and the server cannot send to it by its Ethernet address as usual
// (ARP does not work yet): the client sets the "broadcast" flag, so that the server answers to everybody.
// Not done: renewing the lease (the address is simply kept), several interfaces, the options that are not needed here.

#include "net.h"

#define OP_REQUEST 1
#define OP_REPLY   2
#define MAGIC      0x63825363u
#define OPT_NETMASK 1
#define OPT_ROUTER  3
#define OPT_DNS     6
#define OPT_REQUESTED_IP 50
#define OPT_LEASE   51
#define OPT_TYPE    53
#define OPT_SERVER  54
#define OPT_PARAMETERS 55
#define OPT_CLIENT_ID  61
#define OPT_END     255
#define DISCOVER 1
#define OFFER    2
#define REQUEST  3
#define NAK      6
#define ACK      5

struct dhcp_result dhcp_last;

static u32 transaction;                                   // identifies our conversation: replies for other ids are not for us
static u8 answer[600];  static int answer_len;            // the last reply that belongs to us
static volatile int answered;
static int bound_port = 0;

static void copy (u8 *to, const u8 *from, int n) { for (int i = 0; i < n; i++) to[i] = from[i]; }

// the data of option 'code' in a message, or 0
static const u8 *option (const u8 *msg, int len, int code, int *option_len) {
  for (int i = 240; i + 1 < len; ) {
    int c = msg[i];
    if (c == OPT_END) break;
    if (c == 0) { i++; continue; }                        // padding
    int l = msg[i + 1];
    if (i + 2 + l > len) break;
    if (c == code) { if (option_len) *option_len = l; return msg + i + 2; }
    i += 2 + l;
  }
  return 0;
}

static int message_type (const u8 *msg, int len) {
  const u8 *t = option (msg, len, OPT_TYPE, 0);
  return t ? t[0] : 0;
}

static void on_datagram (u32 src, u16 src_port, const u8 *data, int len) {      // UDP port 68: DHCP replies
  if (src_port != 67 || len < 241 || len > (int)sizeof answer || data[0] != OP_REPLY) return;
  if (get32 (data + 4) != transaction || get32 (data + 236) != MAGIC) return;
  copy (answer, data, len);  answer_len = len;  answered = 1;
}

// DISCOVER (type 1) or REQUEST (type 3, with the address that was offered and the server that offered it)
static void send_message (int type, u32 wanted, u32 server) {
  u8 m[300];
  for (int i = 0; i < (int)sizeof m; i++) m[i] = 0;
  m[0] = OP_REQUEST;  m[1] = 1;  m[2] = 6;                                       // Ethernet, 6 bytes of hardware address
  put32 (m + 4, transaction);
  put16 (m + 10, 0x8000);                                                        // flags: please answer by broadcast
  copy (m + 28, net_dev_mac (), 6);
  put32 (m + 236, MAGIC);
  u8 *o = m + 240;
  *o++ = OPT_TYPE;  *o++ = 1;  *o++ = type;
  *o++ = OPT_CLIENT_ID;  *o++ = 7;  *o++ = 1;  copy (o, net_dev_mac (), 6);  o += 6;       // "I am Ethernet card 52:54:..."
  if (type == REQUEST) {
    *o++ = OPT_REQUESTED_IP;  *o++ = 4;  put32 (o, wanted);  o += 4;
    *o++ = OPT_SERVER;  *o++ = 4;  put32 (o, server);  o += 4;
  }
  *o++ = OPT_PARAMETERS;  *o++ = 4;  *o++ = OPT_NETMASK;  *o++ = OPT_ROUTER;  *o++ = OPT_DNS;  *o++ = OPT_LEASE;     // what we would like to know
  *o++ = OPT_END;
  udp_send (0xffffffff, 67, 68, m, (int)(o - m));
}

// wait up to 'ms' for a reply of the given kind (types are OFFER, or ACK/NAK for 'kind' == ACK); 0 = got it
static int wait_for (int kind, int ms, void (*idle) (void)) {
  u64 start = net_now_ms ();
  while (net_now_ms () - start < (u64)ms) {
    answered = 0;
    net_poll_once ();
    if (answered) {
      int type = message_type (answer, answer_len);
      if (type == kind || (kind == ACK && type == NAK)) return 0;
    }
    if (idle) idle ();
  }
  return -1;
}

int dhcp_run (int timeout_ms, void (*idle) (void)) {
  u32 old_ip = net_my_ip;
  if (!bound_port) { udp_bind (68, on_datagram); bound_port = 1; }
  const u8 *mac = net_dev_mac ();
  transaction = (u32)net_now_ms () * 2654435761u ^ (mac[4] << 8 | mac[5]);      // hard to guess, different for every start
  net_my_ip = 0;                                                                 // we have no address until the server says so

  u64 deadline = net_now_ms () + (u64)timeout_ms;
  int result = -1;
  for (int round = 0; round < 4 && net_now_ms () < deadline; round++) {
    send_message (DISCOVER, 0, 0);
    if (wait_for (OFFER, 1000 << round, idle) != 0) continue;                    // no offer: ask again, waiting a little longer each time
    u32 offered = get32 (answer + 16);
    const u8 *s = option (answer, answer_len, OPT_SERVER, 0);
    u32 server = s ? get32 (s) : get32 (answer + 20);

    send_message (REQUEST, offered, server);
    if (wait_for (ACK, 2000, idle) != 0) continue;
    if (message_type (answer, answer_len) != ACK) continue;                      // NAK: the server has changed its mind

    dhcp_last.server = server;
    dhcp_last.address = get32 (answer + 16);
    int l;
    const u8 *o;
    dhcp_last.netmask = (o = option (answer, answer_len, OPT_NETMASK, &l)) && l >= 4 ? get32 (o) : IP (255, 255, 255, 0);
    dhcp_last.gateway = (o = option (answer, answer_len, OPT_ROUTER, &l)) && l >= 4 ? get32 (o) : 0;
    dhcp_last.dns = (o = option (answer, answer_len, OPT_DNS, &l)) && l >= 4 ? get32 (o) : 0;
    dhcp_last.lease_seconds = (o = option (answer, answer_len, OPT_LEASE, &l)) && l >= 4 ? get32 (o) : 0;
    net_my_ip = dhcp_last.address;
    net_netmask = dhcp_last.netmask;
    if (dhcp_last.gateway) net_gateway = dhcp_last.gateway;
    if (dhcp_last.dns) net_dns = dhcp_last.dns;
    result = 0;
    break;
  }
  if (result != 0) net_my_ip = old_ip;                                           // nobody answered: the old address stays
  return result;
}
