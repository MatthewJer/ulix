// Ulix ARM64 -- virtio-mmio block device driver (polling)
// Replaces the x86 IDE (PIO) and floppy (DMA) drivers: the same read/write-sectors
// interface, but the "disk" is a virtio-blk device on the qemu "virt" machine.
// Supports the legacy (version 1) and the modern (version 2) MMIO transport.

#include "ulix.h"
#include "plat.h"
#include "gic.h"

#define VIRTIO_BASE     (KERNEL_VA + 0x0a000000UL)   // 32 slots of 0x200 bytes
#define VIRTIO_SLOTS    32
#define VIRTIO_SLOT_SZ  0x200

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
#define R_CONFIG        0x100       // virtio-blk: capacity (u64, in 512-byte sectors)

#define ST_ACK          1
#define ST_DRIVER       2
#define ST_DRIVER_OK    4
#define ST_FEATURES_OK  8

#define VIRTQ_DESC_F_NEXT   1
#define VIRTQ_DESC_F_WRITE  2

#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1

#define QSIZE 8

struct desc  { u64 addr; u32 len; unsigned short flags, next; };
struct avail { unsigned short flags, idx, ring[QSIZE]; };
struct used  { unsigned short flags, idx; struct { u32 id, len; } ring[QSIZE]; };

static volatile u8 *dev;                    // MMIO base of the block device
static volatile struct desc  *desc;
static volatile struct avail *avail;
static volatile struct used  *used;
static unsigned short used_seen = 0;
static u64 req_pa;                          // one frame: request header + status byte
static u64 capacity = 0;                    // in sectors

static u32  rd (int off)          { return *(volatile u32 *)(dev + off); }
static void wr (int off, u32 v)   { *(volatile u32 *)(dev + off) = v; }

// Cache maintenance for DMA: a device that is not cache coherent reads and writes main memory only.  Before it reads
// something the CPU wrote, the data cache lines are cleaned; before the CPU reads what the device wrote, they are
// cleaned and invalidated.  This is done always: on a coherent system it only costs a little time.
static void cache_clean (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc cvac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}
static void cache_flush_inval (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc civac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}

int blk_irq = 0;                            // interrupt number of the device (0: none known)
u64 blk_capacity (void) { return capacity; }

int blk_init (void) {
  int slots = plat.nvirtio ? plat.nvirtio : VIRTIO_SLOTS;                      // without a device tree: the 32 slots of qemu virt
  for (int i = 0; i < slots; i++) {
    volatile u8 *s = (volatile u8 *)(plat.nvirtio ? KERNEL_VA + plat.virtio[i] : VIRTIO_BASE + i * VIRTIO_SLOT_SZ);
    if (*(volatile u32 *)(s + R_MAGIC) == 0x74726976 && *(volatile u32 *)(s + R_DEVICE_ID) == 2) {
      dev = s;
      if (plat.nvirtio) blk_irq = plat.virtio_irq[i];
      printf ("virtio: block device in MMIO slot %d (transport version %u, interrupt %d)\n", i, rd (R_VERSION), blk_irq);
      break;
    }
  }
  if (!dev) { printf ("virtio: no block device found\n"); return -1; }
  u32 version = rd (R_VERSION);

  wr (R_STATUS, 0);                         // reset
  wr (R_STATUS, ST_ACK);
  wr (R_STATUS, ST_ACK | ST_DRIVER);
  if (version == 2) {                       // modern devices must be told we speak VERSION_1
    wr (R_DRV_FEAT_SEL, 0);  wr (R_DRV_FEATURES, 0);
    wr (R_DRV_FEAT_SEL, 1);  wr (R_DRV_FEATURES, 1);        // bit 32
    wr (R_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK);
    if (!(rd (R_STATUS) & ST_FEATURES_OK)) { printf ("virtio: features rejected\n"); return -1; }
  } else {
    wr (R_DRV_FEAT_SEL, 0);  wr (R_DRV_FEATURES, 0);
  }

  wr (R_QUEUE_SEL, 0);
  if (rd (R_QUEUE_NUM_MAX) < QSIZE) { printf ("virtio: queue too small\n"); return -1; }
  wr (R_QUEUE_NUM, QSIZE);
  u64 q = alloc_frames_contig (2);          // desc + avail in page 0, used ring in page 1
  desc  = KVA (q);
  avail = KVA (q + sizeof (struct desc) * QSIZE);
  used  = KVA (q + PAGE_SIZE);
  if (version == 1) {
    wr (R_GUEST_PGSIZE, PAGE_SIZE);
    wr (R_QUEUE_ALIGN, PAGE_SIZE);
    wr (R_QUEUE_PFN, q / PAGE_SIZE);
  } else {
    u64 a = q + sizeof (struct desc) * QSIZE, u = q + PAGE_SIZE;
    wr (R_DESC_LO,  (u32)q);  wr (R_DESC_HI,  q >> 32);
    wr (R_AVAIL_LO, (u32)a);  wr (R_AVAIL_HI, a >> 32);
    wr (R_USED_LO,  (u32)u);  wr (R_USED_HI,  u >> 32);
    wr (R_QUEUE_READY, 1);
  }
  wr (R_STATUS, rd (R_STATUS) | ST_DRIVER_OK);

  req_pa = alloc_frame ();
  capacity = rd (R_CONFIG) | ((u64)rd (R_CONFIG + 4) << 32);
  printf ("virtio: disk with %lu sectors (%lu KByte)\n", capacity, capacity / 2);
  if (blk_irq) gic_enable (blk_irq);        // transfers can be waited for with a sleeping process (see glue.c)
  return 0;
}

// one request = 3 descriptors: header (device reads), data, status byte (device writes)
// A request is submitted, the device works on it, and later the CPU finds it in the used ring.  The block driver
// glue (glue.c) lets a process sleep in between and wakes it from the interrupt; blk_read and blk_write poll.
#define R_INTERRUPT_STATUS 0x060
#define R_INTERRUPT_ACK    0x064

static void *cur_buf;  static u32 cur_len;  static int cur_type;

int blk_submit (int type, u64 sector, void *buf, u32 nsectors) {
  if (!dev || sector + nsectors > capacity) return -1;
  u32 *hdr = KVA (req_pa);
  volatile u8 *status = (volatile u8 *)KVA (req_pa + 16);
  hdr[0] = type; hdr[1] = 0; hdr[2] = (u32)sector; hdr[3] = sector >> 32;
  *status = 0xff;

  desc[0].addr = req_pa;       desc[0].len = 16;
  desc[0].flags = VIRTQ_DESC_F_NEXT;  desc[0].next = 1;
  desc[1].addr = PA (buf);     desc[1].len = nsectors * 512;
  desc[1].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  desc[1].next = 2;
  desc[2].addr = req_pa + 16;  desc[2].len = 1;
  desc[2].flags = VIRTQ_DESC_F_WRITE; desc[2].next = 0;

  avail->ring[avail->idx % QSIZE] = 0;      // head of the chain
  __asm__ volatile ("dmb ish" ::: "memory");
  avail->idx++;
  __asm__ volatile ("dmb ish" ::: "memory");
  cur_buf = buf;  cur_len = nsectors * 512UL;  cur_type = type;
  cache_clean ((const void *)desc, sizeof (struct desc) * QSIZE);               // everything the device reads
  cache_clean ((const void *)avail, sizeof (struct avail));
  cache_clean (hdr, 16);
  if (type == VIRTIO_BLK_T_OUT) cache_clean (buf, cur_len);
  else                          cache_flush_inval (buf, cur_len);                // (no dirty line may be written back over the data later)
  cache_flush_inval ((const void *)status, 1);
  wr (R_QUEUE_NOTIFY, 0);
  return 0;
}

int blk_done (void) {                       // has the device finished the request?
  cache_flush_inval ((const void *)used, sizeof (struct used));
  return used->idx != used_seen;
}

int blk_finish (void) {                     // 0 = request done and successful
  volatile u8 *status = (volatile u8 *)KVA (req_pa + 16);
  used_seen++;
  if (cur_type == VIRTIO_BLK_T_IN) cache_flush_inval (cur_buf, cur_len);       // see the data the device wrote
  cache_flush_inval ((const void *)status, 1);
  return *status == 0 ? 0 : -1;
}

void blk_irq_ack (void) {                   // in the interrupt handler: the device stops signalling
  if (dev) wr (R_INTERRUPT_ACK, rd (R_INTERRUPT_STATUS));
}

static int blk_request (int type, u64 sector, void *buf, u32 nsectors) {
  if (blk_submit (type, sector, buf, nsectors) != 0) return -1;
  while (!blk_done ()) __asm__ volatile ("dmb ish" ::: "memory");              // polling
  return blk_finish ();
}

int blk_read  (u64 sector, void *buf, u32 n) { return blk_request (VIRTIO_BLK_T_IN,  sector, buf, n); }
int blk_write (u64 sector, void *buf, u32 n) { return blk_request (VIRTIO_BLK_T_OUT, sector, buf, n); }
