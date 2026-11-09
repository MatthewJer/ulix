#include "net.h"

#define DNS_TYPE_A    1
#define DNS_CLASS_IN  1
#define DNS_SOURCE_PORT 5300                              // the port that the answers come back to

u16 dns_port = 53;

static u16 query_id;                                      // the question that we wait for the answer to
static u8  answer[512];  static int answer_len;           // an answer to it (512 bytes is all that UDP DNS can carry without extensions)
static volatile int answered;
static int bound = 0, busy = 0;                           // the port is bound; a question is on its way (there is only one answer buffer)


static int skip_name (const u8 *msg, int len, int i) {
  while (i < len) {
    int c = msg[i];
    if (c == 0) return i + 1;                             // the end of the name
    if ((c & 0xC0) == 0xC0) return i + 2 <= len ? i + 2 : -1;     // a pointer: the name goes on elsewhere, but ends here
    i += 1 + c;                                           // a label: its length byte and the letters
  }
  return -1;
}


static int build_query (u8 *q, const char *name) {
  put16 (q, query_id);  put16 (q + 2, 0x0100);            // flags: recursion desired
  put16 (q + 4, 1);  put16 (q + 6, 0);  put16 (q + 8, 0);  put16 (q + 10, 0);
  u8 *p = q + 12;
  int total = 0;
  while (*name) {
    int n = 0;
    while (name[n] && name[n] != '.') n++;
    if (n == 0 || n > 63) return -1;
    total += n + 1;
    if (total > 253) return -1;
    *p++ = n;
    for (int i = 0; i < n; i++) *p++ = name[i];
    name += n;
    if (*name == '.') name++;                             // (a dot at the end, "example.org.", is fine)
  }
  if (p == q + 12) return -1;                             // no name at all
  *p++ = 0;                                               // the end of the name
  put16 (p, DNS_TYPE_A);  put16 (p + 2, DNS_CLASS_IN);
  return (int)(p + 4 - q);
}


static int parse_answer (const u8 *m, int len, u32 *ip) {
  if (len < 12 || get16 (m) != query_id || !(m[2] & 0x80)) return DNS_FAILED;      // too short, not our id, not an answer (QR bit)
  int rcode = m[3] & 15;
  if (rcode == 3) return DNS_NOT_FOUND;
  if (rcode != 0 || get16 (m + 4) != 1) return DNS_FAILED;
  int i = skip_name (m, len, 12);                                                  // the question comes back first: skip it
  if (i < 0) return DNS_FAILED;
  i += 4;                                                                          // its type and class
  for (int n = get16 (m + 6); n > 0; n--) {                                        // the answers
    i = skip_name (m, len, i);
    if (i < 0 || i + 10 > len) return DNS_FAILED;
    int type = get16 (m + i), class = get16 (m + i + 2), length = get16 (m + i + 8);
    i += 10;
    if (i + length > len) return DNS_FAILED;
    if (type == DNS_TYPE_A && class == DNS_CLASS_IN && length == 4) { *ip = get32 (m + i); return 0; }
    i += length;                                                                   // a CNAME or something else: next one
  }
  return DNS_FAILED;
}


static void on_datagram (u32 src, u16 src_port, const u8 *data, int len) {
  if (src != net_dns || src_port != dns_port || len < 12 || len > (int)sizeof answer || get16 (data) != query_id) return;
  for (int i = 0; i < len; i++) answer[i] = data[i];
  answer_len = len;  answered = 1;
}

int dns_resolve (const char *name, u32 *ip, void (*idle) (void)) {
  u8 query[12 + 256 + 4];
  if (!net_dns) return DNS_FAILED;                                                 // nobody to ask
  while (busy) { net_poll_once ();  if (idle) idle (); }                           // (another program is resolving: wait for our turn)
  busy = 1;
  if (!bound) { udp_bind (DNS_SOURCE_PORT, on_datagram);  bound = 1; }
  query_id = (u16)(net_now_ms () * 2654435761u >> 8);                              // different every time
  int length = build_query (query, name);
  int result = length < 0 ? DNS_FAILED : -1;
  for (int attempt = 0; attempt < 3 && length > 0; attempt++) {                    // three questions: after 1, 2 and 4 seconds we give up
    answered = 0;
    udp_send (net_dns, dns_port, DNS_SOURCE_PORT, query, length);
    u64 start = net_now_ms ();
    while (!answered && net_now_ms () - start < (u64)(1000 << attempt)) { net_poll_once ();  if (!answered && idle) idle (); }
    if (!answered) continue;
    result = parse_answer (answer, answer_len, ip);
    if (result == 0 || result == DNS_NOT_FOUND) break;
    result = -1;
  }
  busy = 0;
  return result;
}

