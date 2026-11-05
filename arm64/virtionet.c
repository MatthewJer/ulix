// Ulix ARM64 -- the network card of the qemu "virt" machine: a virtio network device (polling)
//
// This is the lowest layer of the network: it moves complete Ethernet frames between memory and the card. It knows nothing about
// ARP, IP or TCP (those are in net.c). It works like the virtio disk driver in virtio.c: the driver and the card share "queues" in
// memory; the driver puts the addresses of buffers into a queue and tells the card, the card works on them and reports which
// buffers are finished. A network card has two queues: queue 0 holds empty buffers for frames that arrive (receive queue), queue 1
// holds frames that are to be sent (transmit queue).
//
// The driver polls: it looks into the queue whenever somebody asks for a frame (net_dev_recv) or waits for a frame to be sent.

#include "ulix.h"
#include "plat.h"

#define R_MAGIC         0x000
#define R_VERSION       0x004
#define R_DEVICE_ID     0x008
#define R_DEV_FEATURES  0x010
#define R_DEV_FEAT_SEL  0x014
#define R_DRV_FEATURES  0x020
#define R_DRV_FEAT_SEL  0x024
#define R_GUEST_PGSIZE  0x028       // legacy
#define R_QUEUE_SEL     0x030
#define R_QUEUE_NUM_MAX 0x034
#define R_QUEUE_NUM     0x038
#define R_QUEUE_ALIGN   0x03c       // legacy
#define R_QUEUE_PFN     0x040       // legacy
#define R_QUEUE_READY   0x044       // modern
#define R_QUEUE_NOTIFY  0x050
#define R_STATUS        0x070
#define R_DESC_LO       0x080       // modern
#define R_DESC_HI       0x084
#define R_AVAIL_LO      0x090
#define R_AVAIL_HI      0x094
#define R_USED_LO       0x0a0
#define R_USED_HI       0x0a4
#define R_CONFIG        0x100       // virtio-net: the MAC address (6 bytes)

#define ST_ACK          1
#define ST_DRIVER       2
#define ST_DRIVER_OK    4
#define ST_FEATURES_OK  8

#define VIRTQ_DESC_F_WRITE  2       // the card writes into this buffer (a received frame)
#define FEATURE_MAC         (1u << 5)   // the card has a MAC address of its own

#define VIRTIO_ID_NET   1
#define QSIZE           16          // buffers in a queue
#define RX_QUEUE        0
#define TX_QUEUE        1
#define BUF_SIZE        2048        // every buffer holds the card's header and one frame (at most 1514 bytes)
#define FRAME_MAX       1514

struct desc  { u64 addr; u32 len; unsigned short flags, next; };
struct avail { unsigned short flags, idx, ring[QSIZE]; };
struct used  { unsigned short flags, idx; struct { u32 id, len; } ring[QSIZE]; };

struct queue {
  volatile struct desc  *desc;
  volatile struct avail *avail;
  volatile struct used  *used;
  unsigned short used_seen;         // how many finished buffers the driver has already taken from the used ring
};

static volatile u8 *dev = 0;        // MMIO base of the card (0: there is none)
static struct queue rxq, txq;
static u64 rx_pa;                   // the 16 receive buffers: one contiguous area of 16 * 2048 bytes
static u64 tx_pa;                   // the transmit buffer (one frame at a time)
static int hdr_len;                 // the card puts a header before every frame: 10 bytes (legacy transport), 12 bytes (modern)
static u8 mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };       // (qemu's default, in case the card does not say)

static u32  rd (int off)        { return *(volatile u32 *)(dev + off); }
static void wr (int off, u32 v) { *(volatile u32 *)(dev + off) = v; }

// The memory the card reads (descriptors, the frame to send) must be written back from the CPU cache first, and what the card has
// written (used ring, received frames) must be read again from memory: qemu's memory is coherent, real hardware often is not.
static void cache_clean (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc cvac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}
static void cache_flush_inval (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc civac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}

// Set up one queue: memory for the three tables, and tell the card where they are.
static void queue_init (struct queue *q, int number, int version) {
  wr (R_QUEUE_SEL, number);
  wr (R_QUEUE_NUM, QSIZE);
  u64 pa = alloc_frames_contig (2);          // descriptors and available ring in page 0, used ring in page 1
  q->desc  = KVA (pa);
  q->avail = KVA (pa + sizeof (struct desc) * QSIZE);
  q->used  = KVA (pa + PAGE_SIZE);
  q->used_seen = 0;
  if (version == 1) {
    wr (R_GUEST_PGSIZE, PAGE_SIZE);
    wr (R_QUEUE_ALIGN, PAGE_SIZE);
    wr (R_QUEUE_PFN, pa / PAGE_SIZE);
  } else {
    u64 a = pa + sizeof (struct desc) * QSIZE, u = pa + PAGE_SIZE;
    wr (R_DESC_LO, (u32)pa);  wr (R_DESC_HI, pa >> 32);
    wr (R_AVAIL_LO, (u32)a);  wr (R_AVAIL_HI, a >> 32);
    wr (R_USED_LO, (u32)u);   wr (R_USED_HI, u >> 32);
    wr (R_QUEUE_READY, 1);
  }
}

// Give buffer number 'id' to the card to receive a frame into.
static void rx_post (int id) {
  rxq.desc[id].addr = rx_pa + (u64)id * BUF_SIZE;
  rxq.desc[id].len = BUF_SIZE;
  rxq.desc[id].flags = VIRTQ_DESC_F_WRITE;
  rxq.desc[id].next = 0;
  cache_clean ((const void *)&rxq.desc[id], sizeof (struct desc));
  rxq.avail->ring[rxq.avail->idx % QSIZE] = id;
  __asm__ volatile ("dmb ish" ::: "memory");
  rxq.avail->idx++;
  cache_clean ((const void *)rxq.avail, sizeof (struct avail));
  wr (R_QUEUE_NOTIFY, RX_QUEUE);
}

// Look for the card among the virtio slots; returns 0 if it was found and initialised.
int net_dev_init (void) {
  int slots = plat.nvirtio ? plat.nvirtio : 32;                  // without a device tree: the 32 slots of qemu virt
  for (int i = slots - 1; i >= 0; i--) {
    volatile u8 *s = (volatile u8 *)(plat.nvirtio ? KERNEL_VA + plat.virtio[i] : KERNEL_VA + 0x0a000000UL + i * 0x200);
    if (*(volatile u32 *)(s + R_MAGIC) == 0x74726976 && *(volatile u32 *)(s + R_DEVICE_ID) == VIRTIO_ID_NET) { dev = s; break; }
  }
  if (!dev) return -1;
  u32 version = rd (R_VERSION);

  wr (R_STATUS, 0);                                              // reset
  wr (R_STATUS, ST_ACK);
  wr (R_STATUS, ST_ACK | ST_DRIVER);
  wr (R_DEV_FEAT_SEL, 0);
  u32 features = rd (R_DEV_FEATURES) & FEATURE_MAC;              // the only thing we want: the MAC address
  wr (R_DRV_FEAT_SEL, 0);  wr (R_DRV_FEATURES, features);
  if (version == 2) {                                            // a modern device must be told that we speak VERSION_1 (bit 32)
    wr (R_DRV_FEAT_SEL, 1);  wr (R_DRV_FEATURES, 1);
    wr (R_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK);
    if (!(rd (R_STATUS) & ST_FEATURES_OK)) { printf ("net: features rejected\n"); dev = 0; return -1; }
  }
  hdr_len = version == 2 ? 12 : 10;

  queue_init (&rxq, RX_QUEUE, version);
  queue_init (&txq, TX_QUEUE, version);
  rx_pa = alloc_frames_contig (QSIZE * BUF_SIZE / PAGE_SIZE);
  tx_pa = alloc_frame ();
  wr (R_STATUS, rd (R_STATUS) | ST_DRIVER_OK);
  for (int i = 0; i < QSIZE; i++) rx_post (i);                   // all receive buffers are handed to the card

  if (features & FEATURE_MAC) for (int i = 0; i < 6; i++) mac[i] = *(volatile u8 *)(dev + R_CONFIG + i);
  printf ("net: virtio network card, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return 0;
}

const u8 *net_dev_mac (void) { return mac; }

// Send one frame (Ethernet header and data, without the checksum at the end: the card adds it). Returns 0 if it has been sent.
int net_drop_tx = 0;                                           // tests: while > 0, a frame to send is thrown away (as if the network lost it)

int net_dev_send (const void *frame, int len) {
  if (!dev || len < 14 || len > FRAME_MAX) return -1;
  if (net_drop_tx > 0) { net_drop_tx--; return 0; }
  u8 *buf = KVA (tx_pa);
  for (int i = 0; i < hdr_len; i++) buf[i] = 0;                  // the card's header: no offloading, nothing special
  for (int i = 0; i < len; i++) buf[hdr_len + i] = ((const u8 *)frame)[i];
  cache_clean (buf, hdr_len + len);

  txq.desc[0].addr = tx_pa;
  txq.desc[0].len = hdr_len + len;
  txq.desc[0].flags = 0;                                         // (the card only reads this buffer)
  txq.desc[0].next = 0;
  cache_clean ((const void *)&txq.desc[0], sizeof (struct desc));
  txq.avail->ring[txq.avail->idx % QSIZE] = 0;
  __asm__ volatile ("dmb ish" ::: "memory");
  txq.avail->idx++;
  cache_clean ((const void *)txq.avail, sizeof (struct avail));
  wr (R_QUEUE_NOTIFY, TX_QUEUE);

  for (long spins = 0; spins < 5000000; spins++) {               // wait until the card says "sent"
    cache_flush_inval ((const void *)txq.used, sizeof (struct used));
    if (txq.used->idx != txq.used_seen) { txq.used_seen++; return 0; }
  }
  printf ("net: the card does not send\n");
  return -1;
}

// Has a frame arrived? If so, copy it to 'out' (at most 'max' bytes) and return its length; 0 if nothing has arrived.
int net_dev_recv (void *out, int max) {
  if (!dev) return 0;
  cache_flush_inval ((const void *)rxq.used, sizeof (struct used));
  if (rxq.used->idx == rxq.used_seen) return 0;
  u32 id = rxq.used->ring[rxq.used_seen % QSIZE].id;
  int len = (int)rxq.used->ring[rxq.used_seen % QSIZE].len - hdr_len;
  rxq.used_seen++;
  u8 *buf = KVA (rx_pa + (u64)id * BUF_SIZE);
  cache_flush_inval (buf, BUF_SIZE);
  if (len > max) len = max;
  for (int i = 0; i < len; i++) ((u8 *)out)[i] = buf[hdr_len + i];
  rx_post (id);                                                  // the buffer goes back to the card
  return len > 0 ? len : 0;
}
