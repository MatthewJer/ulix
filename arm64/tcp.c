// Ulix ARM64 -- a small TCP
//
// TCP gives two programs a reliable stream of bytes in both directions, on top of IP packets that can get lost, arrive twice or in
// the wrong order. The ideas, in the order in which they appear in this file:
//
//   * Every byte has a sequence number. A segment says "my first byte has number SEQ" and "I have received everything before number
//     ACK from you". The numbers of the two directions are independent. SYN (opening) and FIN (closing) each use up one number.
//   * The sender keeps what it has sent until it is acknowledged, and sends it again if no acknowledgement arrives in time
//     (retransmission). The receiver only accepts the bytes that come next in the sequence; everything else it ignores and repeats
//     which number it expects, so that the sender notices.
//   * The receiver tells how much room it has left in every segment (the "window"); the sender never has more bytes on the way.
//   * A connection goes through states: first the "three-way handshake" (SYN, SYN+ACK, ACK), then data, then both sides close
//     with a FIN each.
//
// What is left out, to keep it readable: congestion control (the sender only respects the receiver's window), the "slow start" and
// "fast retransmit" tricks, selective acknowledgements, window scaling and every other option except the segment size, urgent data,
// and zero window probes. Segments that arrive out of order are dropped, not kept. That is enough for a telnet session.

#include "net.h"

#define BUFFER      4096                  // bytes of the receive buffer and of the send buffer of every connection
#define CONNS       8
#define LISTENERS   4
#define MSS         1460                  // the largest segment we send or accept (Ethernet 1500 - IP 20 - TCP 20)
#define RTO_MS      250                   // how long to wait for an acknowledgement before sending again (doubles with every retry)
#define MAX_RETRIES 8
#define TIME_WAIT_MS 1000                 // the closing side stays a moment in TIME_WAIT (RFC 793: twice the segment lifetime)

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

enum { CLOSED, SYN_RCVD, SYN_SENT, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT };

struct tcp_conn {
  int state;
  u32 remote_ip;  u16 local_port, remote_port;
  int from_listener, accepted;            // opened by a listening port; has tcp_accept () returned it yet?

  u32 snd_una;                            // the oldest sequence number that has been sent but not acknowledged
  u32 snd_nxt;                            // the next sequence number to send
  u32 rcv_nxt;                            // the next sequence number we expect from the other side
  u32 peer_window;                        // how many bytes the other side can take (from its last segment)
  u32 mss;                                // the largest segment the other side accepts

  u8  tx[BUFFER];  u32 tx_pos, tx_len;    // the send buffer (a ring): tx_len bytes, the first one has sequence number snd_una
  u8  rx[BUFFER];  u32 rx_pos, rx_len;    // the receive buffer (a ring): bytes that have arrived and wait for tcp_read ()
  int fin_pending, fin_sent, fin_received;       // we want to close / our FIN has been sent / the other side's FIN has arrived

  u64 timer_at;  int retries;             // retransmission timer: when it runs out (0: not running), how many times it has
  u64 time_wait_until;
};

static struct tcp_conn conns[CONNS];
static u16 listeners[LISTENERS];
static u16 next_port = 49152;             // local ports of connections that we open (the "ephemeral" ports)

static int seq_lt (u32 a, u32 b) { return (int)(a - b) < 0; }                // sequence numbers wrap around: compare the difference
static int seq_le (u32 a, u32 b) { return (int)(a - b) <= 0; }
static u32 minimum (u32 a, u32 b) { return a < b ? a : b; }

// ---------------------------------------------------------------- sending segments
//     | source port (2) | destination port (2) | sequence number (4) | acknowledgement number (4) |
//     | header length in words (4 bits), 0 (4 bits), flags (1) | window (2) | checksum (2) | urgent pointer (2) | options | data

static u32 free_window (tcp_conn *c) { return BUFFER - c->rx_len; }

// build and send one segment; 'data' may be 0. The data comes in two parts because it may wrap around the end of a ring buffer.
static void emit (u32 dst_ip, u16 src_port, u16 dst_port, u32 seq, u32 ack, int flags, u32 window, const u8 *d1, int n1, const u8 *d2, int n2) {
  static u8 seg[20 + 4 + MSS];
  int header = (flags & SYN) ? 24 : 20;                            // a SYN carries the option "maximum segment size"
  put16 (seg, src_port);  put16 (seg + 2, dst_port);  put32 (seg + 4, seq);  put32 (seg + 8, ack);
  seg[12] = (header / 4) << 4;  seg[13] = flags;  put16 (seg + 14, window);  put16 (seg + 16, 0);  put16 (seg + 18, 0);
  if (flags & SYN) { seg[20] = 2;  seg[21] = 4;  put16 (seg + 22, MSS); }
  for (int i = 0; i < n1; i++) seg[header + i] = d1[i];
  for (int i = 0; i < n2; i++) seg[header + n1 + i] = d2[i];
  int len = header + n1 + n2;
  put16 (seg + 16, net_checksum (seg, len, net_pseudo_sum (net_my_ip, dst_ip, 6, len)));
  ip_send (dst_ip, 6, seg, len);
}

static void send_flags (tcp_conn *c, u32 seq, int flags) {
  emit (c->remote_ip, c->local_port, c->remote_port, seq, (flags & ACK) ? c->rcv_nxt : 0, flags, free_window (c), 0, 0, 0, 0);
}

static void arm_timer (tcp_conn *c) { c->timer_at = net_now_ms () + ((u64)RTO_MS << c->retries); }

// Send what the window allows: first new data from the send buffer, then (when all data is out and the program has closed) our FIN.
static void output (tcp_conn *c) {
  if (c->state == CLOSED || c->state == SYN_SENT || c->state == SYN_RCVD || c->state == TIME_WAIT) return;
  for (;;) {
    u32 in_flight = c->snd_nxt - c->snd_una;                      // sent, not acknowledged (the FIN counts as one)
    u32 data_sent = in_flight - (c->fin_sent ? 1 : 0);
    u32 unsent = c->tx_len - data_sent;
    u32 room = c->peer_window > in_flight ? c->peer_window - in_flight : 0;
    u32 n = minimum (minimum (unsent, room), c->mss);
    if (n == 0) break;
    u32 from = (c->tx_pos + data_sent) % BUFFER;
    u32 first = minimum (n, BUFFER - from);                       // the ring wraps: one or two pieces
    emit (c->remote_ip, c->local_port, c->remote_port, c->snd_nxt, c->rcv_nxt, ACK | PSH, free_window (c), c->tx + from, first, c->tx, n - first);
    c->snd_nxt += n;
    if (!c->timer_at) arm_timer (c);
  }
  u32 data_sent = c->snd_nxt - c->snd_una - (c->fin_sent ? 1 : 0);
  if (c->fin_pending && !c->fin_sent && data_sent == c->tx_len) {
    send_flags (c, c->snd_nxt, FIN | ACK);
    c->snd_nxt++;  c->fin_sent = 1;
    if (!c->timer_at) arm_timer (c);
  }
}

// ---------------------------------------------------------------- connections
static tcp_conn *find (u32 ip, u16 remote_port, u16 local_port) {
  for (int i = 0; i < CONNS; i++)
    if (conns[i].state != CLOSED && conns[i].remote_ip == ip && conns[i].remote_port == remote_port && conns[i].local_port == local_port)
      return &conns[i];
  return 0;
}

static tcp_conn *allocate (void) {
  for (int i = 0; i < CONNS; i++)
    if (conns[i].state == CLOSED) {
      u8 *p = (u8 *)&conns[i];
      for (unsigned k = 0; k < sizeof conns[i]; k++) p[k] = 0;
      return &conns[i];
    }
  return 0;
}

static u32 new_sequence (void) { return (u32)net_now_ms () * 250000 + 12345; }       // (the first sequence number should be hard to guess)

int tcp_listen (u16 port) {
  for (int i = 0; i < LISTENERS; i++) if (!listeners[i]) { listeners[i] = port; return 0; }
  return -1;
}

tcp_conn *tcp_connect (u32 ip, u16 port) {
  tcp_conn *c = allocate ();
  if (!c) return 0;
  c->remote_ip = ip;  c->remote_port = port;  c->local_port = next_port++;
  c->snd_una = new_sequence ();  c->snd_nxt = c->snd_una + 1;  c->mss = 536;  c->peer_window = 0;
  c->state = SYN_SENT;
  emit (ip, c->local_port, port, c->snd_una, 0, SYN, free_window (c), 0, 0, 0, 0);
  arm_timer (c);
  return c;
}

tcp_conn *tcp_accept (u16 port) {
  for (int i = 0; i < CONNS; i++)
    if (conns[i].state >= ESTABLISHED && conns[i].from_listener && !conns[i].accepted && conns[i].local_port == port) {
      conns[i].accepted = 1;
      return &conns[i];
    }
  return 0;
}

int tcp_established (tcp_conn *c) { return c->state == ESTABLISHED || c->state == CLOSE_WAIT; }
int tcp_closed (tcp_conn *c) { return c->state == CLOSED; }

int tcp_read (tcp_conn *c, u8 *buf, int max) {
  u32 before = free_window (c);
  int n = (int)minimum (c->rx_len, (u32)max);
  for (int i = 0; i < n; i++) buf[i] = c->rx[(c->rx_pos + i) % BUFFER];
  c->rx_pos = (c->rx_pos + n) % BUFFER;
  c->rx_len -= n;
  if (n > 0 && before < c->mss && free_window (c) >= c->mss) send_flags (c, c->snd_nxt, ACK);     // tell the sender that there is room again
  if (n == 0 && (c->fin_received || c->state == CLOSED)) return -1;
  return n;
}

int tcp_write (tcp_conn *c, const u8 *data, int len) {
  if (c->state != ESTABLISHED && c->state != CLOSE_WAIT) return -1;
  int n = (int)minimum ((u32)len, BUFFER - c->tx_len);
  for (int i = 0; i < n; i++) c->tx[(c->tx_pos + c->tx_len + i) % BUFFER] = data[i];
  c->tx_len += n;
  output (c);
  return n;
}

void tcp_close (tcp_conn *c) {
  if (c->state == ESTABLISHED) c->state = FIN_WAIT_1;
  else if (c->state == CLOSE_WAIT) c->state = LAST_ACK;
  else if (c->state == SYN_SENT || c->state == SYN_RCVD) { c->state = CLOSED; return; }
  else return;
  c->fin_pending = 1;
  output (c);
}

// ---------------------------------------------------------------- segments that arrive
static void reset_for (u32 src, const u8 *seg, int len, int header) {       // a segment for a connection that does not exist: tell the sender
  int flags = seg[13];
  if (flags & RST) return;
  u16 sport = get16 (seg), dport = get16 (seg + 2);
  if (flags & ACK) emit (src, dport, sport, get32 (seg + 8), 0, RST, 0, 0, 0, 0, 0);
  else {
    u32 consumed = (len - header) + ((flags & SYN) ? 1 : 0) + ((flags & FIN) ? 1 : 0);
    emit (src, dport, sport, 0, get32 (seg + 4) + consumed, RST | ACK, 0, 0, 0, 0, 0);
  }
}

void tcp_input (u32 src, const u8 *seg, int len) {
  if (len < 20 || net_checksum (seg, len, net_pseudo_sum (src, net_my_ip, 6, len)) != 0) return;
  int header = (seg[12] >> 4) * 4, flags = seg[13];
  if (header < 20 || header > len) return;
  u16 sport = get16 (seg), dport = get16 (seg + 2);
  u32 seq = get32 (seg + 4), ack = get32 (seg + 8), window = get16 (seg + 14);
  const u8 *data = seg + header;
  int dlen = len - header;

  tcp_conn *c = find (src, sport, dport);
  if (!c) {
    int listening = 0;
    for (int i = 0; i < LISTENERS; i++) if (listeners[i] == dport) listening = 1;
    if ((flags & SYN) && !(flags & ACK) && listening && (c = allocate ())) {      // a new connection: answer with SYN+ACK
      c->remote_ip = src;  c->remote_port = sport;  c->local_port = dport;  c->from_listener = 1;
      c->rcv_nxt = seq + 1;  c->snd_una = new_sequence ();  c->snd_nxt = c->snd_una + 1;
      c->peer_window = window;  c->mss = 536;
      for (int o = 20; o + 1 < header && seg[o] != 0; ) {                         // the options: only the maximum segment size is of interest
        if (seg[o] == 1) { o++; continue; }
        if (seg[o] == 2 && seg[o + 1] == 4 && o + 4 <= header) c->mss = minimum (get16 (seg + o + 2), MSS);
        o += seg[o + 1] < 2 ? 2 : seg[o + 1];
      }
      c->state = SYN_RCVD;
      send_flags (c, c->snd_una, SYN | ACK);
      arm_timer (c);
    } else {
      reset_for (src, seg, len, header);
    }
    return;
  }

  if (flags & RST) {                                                              // the other side has dropped the connection
    c->state = CLOSED;
    return;
  }

  if (c->state == SYN_SENT) {                                                     // waiting for SYN+ACK
    if ((flags & (SYN | ACK)) != (SYN | ACK) || ack != c->snd_nxt) { reset_for (src, seg, len, header); return; }
    c->rcv_nxt = seq + 1;  c->snd_una = ack;  c->peer_window = window;  c->timer_at = 0;  c->retries = 0;
    for (int o = 20; o + 1 < header && seg[o] != 0; ) {
      if (seg[o] == 1) { o++; continue; }
      if (seg[o] == 2 && seg[o + 1] == 4 && o + 4 <= header) c->mss = minimum (get16 (seg + o + 2), MSS);
      o += seg[o + 1] < 2 ? 2 : seg[o + 1];
    }
    c->state = ESTABLISHED;
    send_flags (c, c->snd_nxt, ACK);
    output (c);
    return;
  }

  if (c->state == SYN_RCVD && (flags & ACK) && ack == c->snd_nxt) {              // the third packet of the handshake
    c->snd_una = ack;  c->timer_at = 0;  c->retries = 0;
    c->state = ESTABLISHED;
  }
  if (c->state == SYN_RCVD) return;                                               // (the SYN was repeated or the ACK is wrong: ignore)

  // ---- acknowledgements: the other side has received bytes, they can leave the send buffer
  if ((flags & ACK) && seq_lt (c->snd_una, ack) && seq_le (ack, c->snd_nxt)) {
    u32 acked = ack - c->snd_una;
    int fin_acked = c->fin_sent && ack == c->snd_nxt;
    if (fin_acked) acked--;                                                       // the last number was our FIN, not a data byte
    acked = minimum (acked, c->tx_len);
    c->tx_pos = (c->tx_pos + acked) % BUFFER;
    c->tx_len -= acked;
    c->snd_una = ack;
    c->retries = 0;
    c->timer_at = 0;
    if (c->snd_una != c->snd_nxt) arm_timer (c);
    if (fin_acked) {
      if (c->state == FIN_WAIT_1) c->state = FIN_WAIT_2;
      else if (c->state == CLOSING) { c->state = TIME_WAIT; c->time_wait_until = net_now_ms () + TIME_WAIT_MS; }
      else if (c->state == LAST_ACK) { c->state = CLOSED; return; }
    }
  }
  if (flags & ACK) c->peer_window = window;

  // ---- data: only the bytes that come next are accepted, all else is dropped and the sender is told what we expect
  int must_ack = 0;
  if (dlen > 0 || (flags & FIN)) {
    if (seq == c->rcv_nxt && (c->state == ESTABLISHED || c->state == FIN_WAIT_1 || c->state == FIN_WAIT_2)) {
      u32 n = minimum ((u32)dlen, free_window (c));
      for (u32 i = 0; i < n; i++) c->rx[(c->rx_pos + c->rx_len + i) % BUFFER] = data[i];
      c->rx_len += n;
      c->rcv_nxt += n;
      if (n == (u32)dlen && (flags & FIN)) {                                      // the other side is done
        c->rcv_nxt++;
        c->fin_received = 1;
        if (c->state == ESTABLISHED) c->state = CLOSE_WAIT;
        else if (c->state == FIN_WAIT_1) c->state = c->fin_sent && c->snd_una == c->snd_nxt ? TIME_WAIT : CLOSING;
        else if (c->state == FIN_WAIT_2) c->state = TIME_WAIT;
        if (c->state == TIME_WAIT) c->time_wait_until = net_now_ms () + TIME_WAIT_MS;
      }
    }
    must_ack = 1;                                                                 // always answer: it tells what we expect next
  }
  if (must_ack) send_flags (c, c->snd_nxt, ACK);
  output (c);
}

// ---------------------------------------------------------------- time
// Called all the time: when an acknowledgement does not arrive in time, everything that is not acknowledged is sent again.
void tcp_timer (void) {
  u64 now = net_now_ms ();
  for (int i = 0; i < CONNS; i++) {
    tcp_conn *c = &conns[i];
    if (c->state == CLOSED) continue;
    if (c->state == TIME_WAIT && now >= c->time_wait_until) { c->state = CLOSED; continue; }
    if (!c->timer_at || now < c->timer_at) continue;
    if (++c->retries > MAX_RETRIES) { c->state = CLOSED; continue; }              // the other side does not answer: give up
    if (c->state == SYN_SENT) emit (c->remote_ip, c->local_port, c->remote_port, c->snd_una, 0, SYN, free_window (c), 0, 0, 0, 0);
    else if (c->state == SYN_RCVD) send_flags (c, c->snd_una, SYN | ACK);
    else { c->snd_nxt = c->snd_una;  c->fin_sent = 0;  output (c); }              // go back to the first unacknowledged byte and send again
    arm_timer (c);
  }
}
