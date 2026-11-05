// Ulix ARM64 -- the "echo" service (port 7): sends back what it gets. The oldest and simplest network service, and so a good test:
// a program on another computer sends something and sees whether the network stack of Ulix gives it back unchanged.
//
// UDP: every datagram is sent back to its sender. TCP: one connection at a time is served: what is read is written back, and when
// the other side closes, we close, too. Where it is started: by the network thread (netd.c) when the kernel command line says
// "netecho", and by the network test (net_test.c).

#include "net.h"

int echo_udp_datagrams = 0, echo_tcp_connections = 0;      // how many were served (the test looks at them)
long echo_tcp_bytes = 0;

static tcp_conn *conn = 0;                                 // the connection that is being served
static u8 pending[1024];  static int pending_len = 0;      // bytes that were read but did not fit into the send buffer yet

static void udp_echo (u32 src, u16 src_port, const u8 *data, int len) {
  udp_send (src, src_port, 7, data, len);
  echo_udp_datagrams++;
}

int echo_start (void) {                                    // 0 = port 7 is open for UDP and TCP
  if (udp_bind (7, udp_echo) != 0) return -1;
  return tcp_listen (7);
}

// Do what there is to do for the TCP part; returns 1 if anything was moved (so the caller looks again at once).
int echo_step (void) {
  int moved = 0;
  if (!conn) conn = tcp_accept (7);
  if (!conn) return 0;
  if (pending_len == 0) {
    int n = tcp_read (conn, pending, sizeof pending);
    if (n > 0) { pending_len = n;  echo_tcp_bytes += n;  moved = 1; }
    else if (n < 0) { tcp_close (conn);  conn = 0;  echo_tcp_connections++;  return 1; }       // the other side has closed: so do we
  }
  if (pending_len > 0) {
    int sent = tcp_write (conn, pending, pending_len);
    if (sent > 0) {                                        // (it may take only a part: the rest stays for the next time)
      for (int i = sent; i < pending_len; i++) pending[i - sent] = pending[i];
      pending_len -= sent;
      moved = 1;
    }
  }
  return moved;
}
