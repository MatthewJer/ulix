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

// One structure per disk (virtio-blk device).  qemu puts the first -device in the LAST free MMIO slot, the second in the one
// before it, so the disks are numbered from the highest address down: disk 0 is /dev/hda, disk 1 is /dev/hdb.
#define MAX_DISKS 2
struct blk {
  volatile u8 *dev;                         // MMIO base of the block device
  volatile struct desc  *desc;
  volatile struct avail *avail;
  volatile struct used  *used;
  unsigned short used_seen;
  u64 req_pa;                               // one frame: request header + status byte
  u64 capacity;                             // in sectors
  int irq;                                  // interrupt number of the device (0: none known)
  void *cur_buf;  u32 cur_len;  int cur_type;
};
static struct blk disks[MAX_DISKS];
int blk_disks = 0;                          // how many were found
int blk_irq = 0;                            // interrupt number of disk 0 (0: none known)

static u32  rd (struct blk *b, int off)          { return *(volatile u32 *)(b->dev + off); }
static void wr (struct blk *b, int off, u32 v)   { *(volatile u32 *)(b->dev + off) = v; }

static void cache_clean (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc cvac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}
static void cache_flush_inval (const void *p, u64 len) {
  for (u64 a = (u64)p & ~63UL; a < (u64)p + len; a += 64) __asm__ volatile ("dc civac, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb sy" ::: "memory");
}

u64 blk_capacity_of (int n) { return n >= 0 && n < blk_disks ? disks[n].capacity : 0; }
u64 blk_capacity (void) { return blk_capacity_of (0); }
int blk_is_irq (int irq) { for (int i = 0; i < blk_disks; i++) if (disks[i].irq && disks[i].irq == irq) return 1; return 0; }

static int init_disk (struct blk *b) {
  u32 version = rd (b, R_VERSION);

  wr (b, R_STATUS, 0);                      // reset
  wr (b, R_STATUS, ST_ACK);
  wr (b, R_STATUS, ST_ACK | ST_DRIVER);
  if (version == 2) {                       // modern devices must be told we speak VERSION_1
    wr (b, R_DRV_FEAT_SEL, 0);  wr (b, R_DRV_FEATURES, 0);
    wr (b, R_DRV_FEAT_SEL, 1);  wr (b, R_DRV_FEATURES, 1);        // bit 32
    wr (b, R_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK);
    if (!(rd (b, R_STATUS) & ST_FEATURES_OK)) { printf ("virtio: features rejected\n"); return -1; }
  } else {
    wr (b, R_DRV_FEAT_SEL, 0);  wr (b, R_DRV_FEATURES, 0);
  }

  wr (b, R_QUEUE_SEL, 0);
  if (rd (b, R_QUEUE_NUM_MAX) < QSIZE) { printf ("virtio: queue too small\n"); return -1; }
  wr (b, R_QUEUE_NUM, QSIZE);
  u64 q = alloc_frames_contig (2);          // desc + avail in page 0, used ring in page 1
  b->desc  = KVA (q);
  b->avail = KVA (q + sizeof (struct desc) * QSIZE);
  b->used  = KVA (q + PAGE_SIZE);
  if (version == 1) {
    wr (b, R_GUEST_PGSIZE, PAGE_SIZE);
    wr (b, R_QUEUE_ALIGN, PAGE_SIZE);
    wr (b, R_QUEUE_PFN, q / PAGE_SIZE);
  } else {
    u64 a = q + sizeof (struct desc) * QSIZE, u = q + PAGE_SIZE;
    wr (b, R_DESC_LO,  (u32)q);  wr (b, R_DESC_HI,  q >> 32);
    wr (b, R_AVAIL_LO, (u32)a);  wr (b, R_AVAIL_HI, a >> 32);
    wr (b, R_USED_LO,  (u32)u);  wr (b, R_USED_HI,  u >> 32);
    wr (b, R_QUEUE_READY, 1);
  }
  wr (b, R_STATUS, rd (b, R_STATUS) | ST_DRIVER_OK);

  b->req_pa = alloc_frame ();
  b->used_seen = 0;
  b->capacity = rd (b, R_CONFIG) | ((u64)rd (b, R_CONFIG + 4) << 32);
  return 0;
}

int blk_init (void) {
  int slots = plat.nvirtio ? plat.nvirtio : VIRTIO_SLOTS;                      // without a device tree: the 32 slots of qemu virt
  volatile u8 *found[MAX_DISKS];  int found_irq[MAX_DISKS], found_slot[MAX_DISKS], n = 0;
  for (int i = slots - 1; i >= 0 && n < MAX_DISKS; i--) {                      // from the highest address down (see above)
    volatile u8 *s = (volatile u8 *)(plat.nvirtio ? KERNEL_VA + plat.virtio[i] : VIRTIO_BASE + i * VIRTIO_SLOT_SZ);
    if (*(volatile u32 *)(s + R_MAGIC) == 0x74726976 && *(volatile u32 *)(s + R_DEVICE_ID) == 2) {
      found[n] = s;  found_slot[n] = i;  found_irq[n] = plat.nvirtio ? plat.virtio_irq[i] : 0;  n++;
    }
  }
  if (n == 0) { printf ("virtio: no block device found\n"); return -1; }
  blk_disks = 0;
  for (int k = 0; k < n; k++) {
    struct blk *b = &disks[k];
    b->dev = found[k];  b->irq = found_irq[k];
    printf ("virtio: block device %d in MMIO slot %d (transport version %u, interrupt %d)\n", k, found_slot[k], rd (b, R_VERSION), b->irq);
    if (init_disk (b) != 0) { if (k == 0) return -1; break; }
    blk_disks++;
    printf ("virtio: disk %d with %lu sectors (%lu KByte)\n", k, b->capacity, b->capacity / 2);
    if (b->irq) gic_enable (b->irq);        // transfers can be waited for with a sleeping process (see glue.c)
  }
  blk_irq = disks[0].irq;
  return 0;
}

// one request = 3 descriptors: header (device reads), data, status byte (device writes)
// A request is submitted, the device works on it, and later the CPU finds it in the used ring.  The block driver
// glue (glue.c) lets a process sleep in between and wakes it from the interrupt; blk_read and blk_write poll.
#define R_INTERRUPT_STATUS 0x060
#define R_INTERRUPT_ACK    0x064

int blk_submit_n (int n, int type, u64 sector, void *buf, u32 nsectors) {
  if (n < 0 || n >= blk_disks) return -1;
  struct blk *b = &disks[n];
  if (sector + nsectors > b->capacity) return -1;
  u32 *hdr = KVA (b->req_pa);
  volatile u8 *status = (volatile u8 *)KVA (b->req_pa + 16);
  hdr[0] = type; hdr[1] = 0; hdr[2] = (u32)sector; hdr[3] = sector >> 32;
  *status = 0xff;

  b->desc[0].addr = b->req_pa;       b->desc[0].len = 16;
  b->desc[0].flags = VIRTQ_DESC_F_NEXT;  b->desc[0].next = 1;
  b->desc[1].addr = PA (buf);     b->desc[1].len = nsectors * 512;
  b->desc[1].flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  b->desc[1].next = 2;
  b->desc[2].addr = b->req_pa + 16;  b->desc[2].len = 1;
  b->desc[2].flags = VIRTQ_DESC_F_WRITE; b->desc[2].next = 0;

  b->avail->ring[b->avail->idx % QSIZE] = 0;      // head of the chain
  __asm__ volatile ("dmb ish" ::: "memory");
  b->avail->idx++;
  __asm__ volatile ("dmb ish" ::: "memory");
  b->cur_buf = buf;  b->cur_len = nsectors * 512UL;  b->cur_type = type;
  cache_clean ((const void *)b->desc, sizeof (struct desc) * QSIZE);               // everything the device reads
  cache_clean ((const void *)b->avail, sizeof (struct avail));
  cache_clean (hdr, 16);
  if (type == VIRTIO_BLK_T_OUT) cache_clean (buf, b->cur_len);
  else                          cache_flush_inval (buf, b->cur_len);                // (no dirty line may be written back over the data later)
  cache_flush_inval ((const void *)status, 1);
  wr (b, R_QUEUE_NOTIFY, 0);
  return 0;
}

int blk_done_n (int n) {                    // has the device finished the request?
  struct blk *b = &disks[n];
  cache_flush_inval ((const void *)b->used, sizeof (struct used));
  return b->used->idx != b->used_seen;
}

int blk_finish_n (int n) {                  // 0 = request done and successful
  struct blk *b = &disks[n];
  volatile u8 *status = (volatile u8 *)KVA (b->req_pa + 16);
  b->used_seen++;
  if (b->cur_type == VIRTIO_BLK_T_IN) cache_flush_inval (b->cur_buf, b->cur_len);       // see the data the device wrote
  cache_flush_inval ((const void *)status, 1);
  return *status == 0 ? 0 : -1;
}

void blk_irq_ack (void) {                   // in the interrupt handler: every device stops signalling
  for (int i = 0; i < blk_disks; i++) wr (&disks[i], R_INTERRUPT_ACK, rd (&disks[i], R_INTERRUPT_STATUS));
}

static int blk_request (int n, int type, u64 sector, void *buf, u32 nsectors) {
  if (blk_submit_n (n, type, sector, buf, nsectors) != 0) return -1;
  while (!blk_done_n (n)) __asm__ volatile ("dmb ish" ::: "memory");              // polling
  return blk_finish_n (n);
}

int blk_read_n  (int n, u64 sector, void *buf, u32 cnt) { return blk_request (n, VIRTIO_BLK_T_IN,  sector, buf, cnt); }
int blk_write_n (int n, u64 sector, void *buf, u32 cnt) { return blk_request (n, VIRTIO_BLK_T_OUT, sector, buf, cnt); }
// disk 0 (the tests and the kernel's own Minix reader use these)
int blk_read  (u64 sector, void *buf, u32 cnt) { return blk_read_n (0, sector, buf, cnt); }
int blk_write (u64 sector, void *buf, u32 cnt) { return blk_write_n (0, sector, buf, cnt); }
int blk_submit (int type, u64 sector, void *buf, u32 nsectors) { return blk_submit_n (0, type, sector, buf, nsectors); }
int blk_done (void)   { return blk_done_n (0); }
int blk_finish (void) { return blk_finish_n (0); }
