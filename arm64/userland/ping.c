#include "../ulixlib.h"
#include "../arm64-extra.h"

// ping -- does a computer answer? (a new program for the AArch64 port; the network is not part of the original Ulix)
//
//     ping [-c count] [-s size] A.B.C.D
//
// ping sends an ICMP "echo request" and waits for the "echo reply" that the other computer sends back, and shows how long that took.
// The message looks like this (RFC 792):
//
//     | type (1) | code (1) | checksum (2) | identifier (2) | sequence number (2) | data ...
//
// type 8 is a request, type 0 a reply. The other side copies identifier, sequence number and data into its reply, so that we can
// tell which request an answer belongs to; the identifier tells our answers from those that another ping program waits for.
// The kernel only provides a "raw ICMP socket" (icmp_open, icmp_send, icmp_recv): the message is built here, including the checksum.

// The checksum: the sum of all 16 bit words, carries added back in, all bits turned around.
static unsigned short checksum (unsigned char *data, int len) {
  unsigned sum = 0;
  for (int i = 0; i + 1 < len; i += 2) sum += (data[i] << 8) | data[i + 1];
  if (len & 1) sum += data[len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (unsigned short)~sum;
}

// "10.0.2.2" -> the address as a number; 0 = ok
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
  if (*s != 0) return -1;
  *address = value;
  return 0;
}

static int number (char *s) {
  int n = 0;
  while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
  return n;
}

static void usage (void) { printf ("usage: ping [-c count] [-s size] A.B.C.D   (a dotted address, for example 10.0.2.2)\n"); }

// microseconds -> "3.4" (milliseconds with one decimal)
static void show_time (unsigned us) { printf ("%d.%d", us / 1000, (us / 100) % 10); }

int main (int argc, char *argv[]) {
  int count = 4, size = 32, i = 1;
  unsigned address = 0;
  int have_address = 0;
  for (; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] == 'c' && i + 1 < argc) count = number (argv[++i]);
    else if (argv[i][0] == '-' && argv[i][1] == 's' && i + 1 < argc) size = number (argv[++i]);
    else if (!have_address && parse_address (argv[i], &address) == 0) have_address = 1;
    else { usage (); return 1; }
  }
  if (!have_address || count < 1 || size < 0 || size > 200) { usage (); return 1; }

  int sock = icmp_open ();
  if (sock < 0) { printf ("ping: cannot open a socket (is there a network card?)\n"); return 1; }

  unsigned char request[8 + 200], reply[256];
  unsigned short id = getpid () & 0xffff;
  int sent = 0, received = 0;
  unsigned rtt_min = 0xffffffff, rtt_max = 0, rtt_sum = 0;

  printf ("PING %d.%d.%d.%d: %d data bytes\n", address >> 24, (address >> 16) & 255, (address >> 8) & 255, address & 255, size);
  for (int seq = 1; seq <= count; seq++) {
    request[0] = 8;  request[1] = 0;  request[2] = 0;  request[3] = 0;           // echo request, checksum still 0
    request[4] = id >> 8;  request[5] = id & 255;  request[6] = seq >> 8;  request[7] = seq & 255;
    for (int k = 0; k < size; k++) request[8 + k] = 'a' + k % 26;
    unsigned short sum = checksum (request, 8 + size);
    request[2] = sum >> 8;  request[3] = sum & 255;

    struct icmp_msg m;
    m.handle = sock;  m.ip = address;  m.buf = (char *)request;  m.len = 8 + size;
    unsigned start = clock_us ();
    if (icmp_send (&m) != 0) { printf ("ping: sending failed (no answer to the question who has this address?)\n"); continue; }
    sent++;

    int answered = 0;
    for (;;) {                                                                    // wait for OUR reply, at most one second
      unsigned waited = (clock_us () - start) / 1000;
      if (waited >= 1000) break;
      m.buf = (char *)reply;  m.len = sizeof reply;  m.timeout_ms = 1000 - waited;
      int n = icmp_recv (&m);
      if (n <= 0) break;                                                          // timeout
      if (n < 8) continue;
      unsigned rtt = clock_us () - start;
      if (reply[0] == 0 && m.ip == address && ((reply[4] << 8) | reply[5]) == id && ((reply[6] << 8) | reply[7]) == seq) {
        printf ("%d bytes from %d.%d.%d.%d: icmp_seq=%d time=", n, m.ip >> 24, (m.ip >> 16) & 255, (m.ip >> 8) & 255, m.ip & 255, seq);
        show_time (rtt);  printf (" ms\n");
        received++;  answered = 1;
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
        rtt_sum += rtt;
        break;
      }
      if (reply[0] == 3 || reply[0] == 11) {                                      // the network says that it cannot deliver the request
        printf ("from %d.%d.%d.%d: %s\n", m.ip >> 24, (m.ip >> 16) & 255, (m.ip >> 8) & 255, m.ip & 255,
                reply[0] == 3 ? "destination unreachable" : "time exceeded");
        answered = 1;
        break;
      }
    }
    if (!answered) printf ("Request timeout for icmp_seq %d\n", seq);
    if (seq < count) while ((clock_us () - start) / 1000 < 1000) syscall1 (__NR_resign);       // one request per second
  }
  icmp_close (sock);

  printf ("--- %d.%d.%d.%d ping statistics ---\n", address >> 24, (address >> 16) & 255, (address >> 8) & 255, address & 255);
  printf ("%d packets transmitted, %d received, %d%% packet loss\n", sent, received, sent ? (sent - received) * 100 / sent : 100);
  if (received) {
    printf ("round-trip min/avg/max = ");  show_time (rtt_min);  printf ("/");  show_time (rtt_sum / received);  printf ("/");  show_time (rtt_max);  printf (" ms\n");
  }
  return received ? 0 : 1;
}
