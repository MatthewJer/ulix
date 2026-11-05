// Ulix ARM64 -- the network: what the layers offer each other and the rest of the kernel (see net.c, tcp.c, virtionet.c)
#ifndef NET_H
#define NET_H
#include "ulix.h"

typedef unsigned short u16;

#define IP(a, b, c, d)  (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))     // an IPv4 address as a 32 bit number

// ---- the network card (netdev.c; the drivers are virtionet.c for qemu and genet.c for the Raspberry Pi 4) ----
// A driver moves complete Ethernet frames between memory and its card, and knows nothing about ARP, IP or TCP.
struct net_driver {
  const char *name;                                // the name of the interface: "vioif0" (NetBSD's name for a virtio card), "genet0"
  int  (*init) (void);                             // 0: the card is there and ready
  const u8 *(*mac) (void);                         // its Ethernet address (6 bytes)
  int  (*send) (const void *frame, int len);       // one Ethernet frame (without the checksum at the end): 0 = sent
  int  (*recv) (void *out, int max);               // the length of a frame that has arrived (copied to out), or 0
  int  (*start_irq) (void);                        // let the card interrupt when frames arrive: its interrupt number (0: it cannot, keep polling)
  void (*ack) (void);                              // in the interrupt handler: tell the card that we have noticed
};
extern const struct net_driver virtio_net_driver, genet_driver;

int  net_dev_init (void);                         // look for a card with the drivers in turn: 0 = found one
const u8 *net_dev_mac (void);
int  net_dev_send (const void *frame, int len);
int  net_dev_recv (void *out, int max);
int  net_dev_start_irq (void);
void net_dev_ack (void);

// ---- the network thread (netd.c): a kernel thread that handles the frames as they arrive, woken by the interrupt of the card ----
void netd_start (void);                           // start it (after the threads are set up and the card is found)
extern int netd_irq;                              // the interrupt number of the card (0 until netd_start () has run, or when polling)
void netd_interrupt (void);                       // in the interrupt handler
void netd_tick (void);                            // every timer tick: wakes the thread now and then, for the timers of TCP
extern int net_echo;                              // the kernel command line said "netecho": answer on port 7 (UDP and TCP) with what comes in

// ---- the echo service (echo.c) ----
int  echo_start (void);                           // open port 7 (UDP and TCP): 0 = ok
int  echo_step (void);                            // serve the TCP connection a bit: 1 = something was moved
extern int echo_udp_datagrams, echo_tcp_connections;
extern long echo_tcp_bytes;

// ---- Ethernet, ARP, IPv4, ICMP, UDP (net.c) ----
extern u32 net_my_ip, net_netmask, net_gateway, net_dns;
extern const char *net_if_name;                   // the name of the interface, as the BSDs call a virtio network card: "vioif0"
int  net_init (void);                             // card, addresses: 0 = the network is up
int  net_poll_once (void);                        // take one frame from the card and handle it: 1 = there was one
void net_run (int ms);                            // handle frames for 'ms' milliseconds
u64  net_now_ms (void);                           // milliseconds since an arbitrary start (the counter of the timer)
int  net_ping (u32 ip, int timeout_ms);           // round trip time in ms, or -1
int  ip_send (u32 dst, u8 protocol, const u8 *data, int len);
u16  net_checksum (const u8 *data, int len, u32 sum);       // the Internet checksum (RFC 1071); 'sum' continues an earlier part
u32  net_pseudo_sum (u32 src, u32 dst, u8 protocol, int length);   // the start of the checksum of a UDP or TCP segment
extern int net_drop_tx;                           // for tests: the next frames that are sent are thrown away instead (a lost packet)

// raw ICMP sockets for programs (net.c; the system calls are in net_syscalls.c)
int  icmp_open (int owner, int (*alive) (int owner));         // a socket number, -1 = none free (a socket of a dead owner is taken over)
void icmp_close (int socket);
int  icmp_send (int socket, u32 ip, const u8 *message, int len);       // an ICMP message that the caller has built, checksum included
int  icmp_get (int socket, u8 *buf, int max, u32 *from);      // the oldest message that arrived: its length, 0 = none

typedef void (*udp_handler) (u32 src_ip, u16 src_port, const u8 *data, int len);
int  udp_bind (u16 port, udp_handler handler);
int  udp_send (u32 dst_ip, u16 dst_port, u16 src_port, const u8 *data, int len);

// ---- configuration (netconf.c, dhcp.c) ----
// The address of this computer comes from one of three places: fixed values in net.c (what qemu's user network expects), the kernel
// command line ("ip=10.0.0.5/24 gw=10.0.0.1 dns=10.0.0.1", or "ip=dhcp"), or later from programs (ifconfig, route, dhclient).
const char *plat_cmdline (void);                  // dt.c
int  net_parse_ip (const char *s, u32 *ip, const char **end);       // "10.0.2.2" -> number; 0 = ok, *end is the character after it
void net_configure_from_command_line (void);     // called once at boot after net_init ()

struct dhcp_result { u32 server, address, netmask, gateway, dns, lease_seconds; };
extern struct dhcp_result dhcp_last;              // what the last successful dhcp_run () got
int  dhcp_run (int timeout_ms, void (*idle) (void));    // ask a DHCP server for an address and use it: 0 = done; 'idle' is called while waiting

// ---- TCP (tcp.c) ----
void tcp_input (u32 src_ip, const u8 *segment, int len);      // called by net.c for every TCP segment that arrives
void tcp_timer (void);                                        // called by net.c all the time: retransmissions and timeouts

typedef struct tcp_conn tcp_conn;                             // one connection (the structure is private to tcp.c)
int  tcp_listen (u16 port);                                   // accept connections to this port: 0 = ok
tcp_conn *tcp_accept (u16 port);                              // a new connection to a listening port, or 0 if none is waiting
tcp_conn *tcp_connect (u32 ip, u16 port);                     // start a connection (tcp_established () tells when it is up), 0 = no room
int  tcp_established (tcp_conn *c);                           // 1 = data can be sent and received
int  tcp_closed (tcp_conn *c);                                // 1 = the connection is gone: do not use the pointer any more
int  tcp_read (tcp_conn *c, u8 *buf, int max);                // bytes received (0: nothing now), -1: the other side has closed (and all was read)
int  tcp_write (tcp_conn *c, const u8 *data, int len);        // bytes accepted into the send buffer (may be fewer than len), -1: cannot send
void tcp_close (tcp_conn *c);                                 // we have nothing more to send

// ---- numbers in the headers are big endian ("network byte order") ----
static inline u16 get16 (const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline u32 get32 (const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static inline void put16 (u8 *p, u16 v) { p[0] = v >> 8; p[1] = v & 0xff; }
static inline void put32 (u8 *p, u32 v) { p[0] = v >> 24; p[1] = (v >> 16) & 0xff; p[2] = (v >> 8) & 0xff; p[3] = v & 0xff; }
#endif
