// Ulix ARM64 -- test of the network (make net-test): runs in its own kernel image (kmain.c with -DNET_MODE) without disk and
// without processes, and talks to tools/net_test.py on the host through qemu's user-mode network: qemu is the router (10.0.2.2) and
// forwards ports of the host to this kernel (the host connects to 127.0.0.1:5556 and 5555, which become 10.0.2.15:7 here).

#include "net.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                              else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

// ---- the echo services on port 7: UDP and TCP send back what they get ----
static int udp_echoed = 0;
static void udp_echo (u32 src, u16 src_port, const u8 *data, int len) {
  udp_send (src, src_port, 7, data, len);
  udp_echoed++;
}

static tcp_conn *echo_conn = 0;           // the connection that is being served (one at a time is enough for the test)
static u8 pending[1024];  static int pending_len = 0;     // bytes that were read but could not be sent back yet (send buffer full)
static int tcp_echoed_connections = 0;
static long tcp_echoed_bytes = 0;

static void tcp_echo_step (void) {
  if (!echo_conn) echo_conn = tcp_accept (7);
  if (!echo_conn) return;
  if (pending_len == 0) {
    int n = tcp_read (echo_conn, pending, sizeof pending);
    if (n > 0) pending_len = n, tcp_echoed_bytes += n;
    else if (n < 0) { tcp_close (echo_conn); echo_conn = 0; tcp_echoed_connections++; return; }     // the host has closed: so do we
  }
  if (pending_len > 0) {
    int sent = tcp_write (echo_conn, pending, pending_len);
    if (sent > 0) {                                           // (it may take only a part: the rest stays for the next round)
      for (int i = sent; i < pending_len; i++) pending[i - sent] = pending[i];
      pending_len -= sent;
    }
  }
}

// run the network until 'done' is true or 'ms' have passed
#define WAIT(cond, ms) do { u64 t0 = net_now_ms (); while (!(cond) && net_now_ms () - t0 < (u64)(ms)) { net_poll_once (); tcp_echo_step (); } } while (0)

int net_test (void) {
  printf ("network test:\n");
  CHECK (net_init () == 0, "virtio network card found and initialised");

  // DHCP: first put a wrong address and router into the kernel, then ask. qemu's user network has a DHCP server (the router, 10.0.2.2),
  // which gives 10.0.2.15, netmask 255.255.255.0, router 10.0.2.2 and the name server 10.0.2.3.
  net_my_ip = IP (192, 168, 77, 5);  net_netmask = IP (255, 255, 0, 0);  net_gateway = IP (192, 168, 77, 1);  net_dns = 0;
  CHECK (dhcp_run (8000, 0) == 0, "DHCP: DISCOVER, OFFER, REQUEST and ACK with the server of qemu");
  CHECK (net_my_ip == IP (10, 0, 2, 15) && net_netmask == IP (255, 255, 255, 0), "DHCP: the address 10.0.2.15 and the netmask 255.255.255.0 are used");
  CHECK (net_gateway == IP (10, 0, 2, 2) && net_dns == IP (10, 0, 2, 3), "DHCP: the router 10.0.2.2 and the name server 10.0.2.3 are used");
  CHECK (dhcp_last.server == IP (10, 0, 2, 2) && dhcp_last.lease_seconds > 0, "DHCP: the answer came from 10.0.2.2 and has a lease time");

  int rtt = net_ping (net_gateway, 3000);
  CHECK (rtt >= 0, "ping of the router: ARP request and reply, ICMP echo request and reply");
  printf ("  info: round trip %d ms\n", rtt);

  const char *hello = "hello from Ulix";
  CHECK (udp_send (IP (10, 0, 2, 2), 5557, 4000, (const u8 *)hello, 15) == 0, "UDP datagram sent to the host (10.0.2.2 is the host, port 5557)");

  CHECK (udp_bind (7, udp_echo) == 0, "UDP port 7 (echo service) bound");
  CHECK (tcp_listen (7) == 0, "TCP port 7 (echo service) is listening");
  printf ("net: ready for the host\n");                    // tools/net_test.py waits for this line
  WAIT (udp_echoed == 3 && tcp_echoed_connections == 2, 60000);
  CHECK (udp_echoed == 3, "three UDP datagrams from the host arrived and were echoed");
  CHECK (tcp_echoed_connections == 2, "two TCP connections from the host were served until the host closed them");
  printf ("  info: %ld bytes echoed over TCP\n", tcp_echoed_bytes);

  // the kernel as the client: connect to the host (port 5558), lose the SYN and the first data segment on the way (they are sent
  // again after the retransmission timeout), send a line, read the answer, close
  net_drop_tx = 1;
  tcp_conn *c = tcp_connect (IP (10, 0, 2, 2), 5558);
  CHECK (c != 0, "tcp_connect to the host (the first SYN is lost on purpose)");
  WAIT (c && tcp_established (c), 10000);
  CHECK (c && tcp_established (c), "the connection is established although the SYN was lost (retransmission)");
  if (c && tcp_established (c)) {
    net_drop_tx = 1;
    CHECK (tcp_write (c, (const u8 *)"ping from Ulix", 14) == 14, "14 bytes accepted for sending (the first segment is lost on purpose)");
    u8 answer[32];  int got = 0, n;
    u64 t0 = net_now_ms ();
    while (got < 4 && net_now_ms () - t0 < 10000) {
      net_poll_once ();
      n = tcp_read (c, answer + got, (int)sizeof answer - got);
      if (n > 0) got += n; else if (n < 0) break;
    }
    CHECK (got == 4 && answer[0] == 'p' && answer[1] == 'o' && answer[2] == 'n' && answer[3] == 'g', "the answer \"pong\" arrived (data was sent again after the loss)");
    tcp_close (c);
    WAIT (tcp_closed (c), 10000);
    CHECK (tcp_closed (c), "the connection was closed by both sides");
  }
  return fails;
}
