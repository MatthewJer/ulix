// Ulix ARM64 -- the SD card of the Raspberry Pi 4B
//
// The card sits on an SD host controller that follows the "SD Host Controller Simplified Specification" (SDHCI); the Pi 4 has
// two of them, and the card slot is on the one called EMMC2. This file talks to the controller in the simplest possible way:
// no interrupts, no DMA, one 512-byte sector per command, the CPU copies every word through the data port ("polling" and PIO,
// like the IDE driver of the book). Register names are those of the specification (FreeBSD's sys/dev/sdhci/sdhci.h, BSD-2-Clause,
// uses the same names; nothing is copied from it).
//
// On the card the MBR partition table says where the disks of Ulix are: every partition of type 0x81 (Minix) is one disk, in
// the order of the table: the first is /dev/hda, the second /dev/hdb, the third stands in for /dev/fd1. Partition 1 is the FAT
// partition with the firmware and the kernel; it is not touched.

#include "ulix.h"
#include "plat.h"

// ---- registers of the controller (offsets from the base address; the width is part of the register) ----
#define R_BLKSIZE    0x04      // 16 bit: block size in bytes
#define R_BLKCNT     0x06      // 16 bit: number of blocks
#define R_ARG        0x08      // 32 bit: command argument
#define R_TRNMOD     0x0c      // 16 bit: transfer mode
#define R_CMD        0x0e      // 16 bit: command (writing it starts the command)
#define R_RESP       0x10      // 4 x 32 bit: response
#define R_DATA       0x20      // 32 bit: data port
#define R_PRESENT    0x24      // 32 bit: present state
#define R_HOSTCTL    0x28      //  8 bit: host control (bus width)
#define R_PWRCTL     0x29      //  8 bit: power control
#define R_CLKCTL     0x2c      // 16 bit: clock control
#define R_TIMEOUT    0x2e      //  8 bit: data timeout
#define R_RESET      0x2f      //  8 bit: software reset
#define R_INT_STATUS 0x30      // 32 bit: low half: normal interrupt status, high half: error interrupt status
#define R_INT_ENABLE 0x34      // 32 bit: which of them can be seen in the status (we poll, so we want to see all)
#define R_SIG_ENABLE 0x38      // 32 bit: which of them raise an interrupt (none)
#define R_CAPS       0x40      // 32 bit: capabilities (bits 15..8: base clock in MHz)

#define PRESENT_CMD_INHIBIT 0x00000001     // a command is still running
#define PRESENT_DAT_INHIBIT 0x00000002     // a data transfer is still running
#define PRESENT_CARD        0x00010000     // a card is in the slot

#define INT_CMD_DONE        0x00000001
#define INT_XFER_DONE       0x00000002
#define INT_WRITE_READY     0x00000010     // the buffer has room for a sector
#define INT_READ_READY      0x00000020     // the buffer holds a sector
#define INT_ERROR           0x00008000     // (and the bits of the high half say which one)

#define CMD_NO_RESPONSE     0x0000
#define CMD_R2              0x0001         // 136 bit response
#define CMD_R1              0x0002         // 48 bit response
#define CMD_R1B             0x0003         // 48 bit response, then the card is busy
#define CMD_CRC_CHECK       0x0008
#define CMD_INDEX_CHECK     0x0010
#define CMD_DATA            0x0020         // data follows (or is sent)

#define TRN_BLKCNT_EN       0x0002
#define TRN_READ            0x0010

#define RESET_ALL           0x01
#define RESET_CMD           0x02
#define RESET_DAT           0x04

static u64 base = 0;                       // virtual address of the controller (0: no SD card)
static u32 rca = 0;                        // the address the card chose for itself
static int high_capacity = 0;              // SDHC/SDXC: the argument of a read is the sector number, otherwise the byte address

static u32  rd32 (int off) { return *(volatile u32 *)(base + off); }
static unsigned short  rd16 (int off) { return *(volatile unsigned short *)(base + off); }
static u8   rd8  (int off) { return *(volatile u8 *)(base + off); }
static void wr32 (int off, u32 v) { *(volatile u32 *)(base + off) = v; }
static void wr16 (int off, unsigned short v) { *(volatile unsigned short *)(base + off) = v; }
static void wr8  (int off, u8 v)  { *(volatile u8 *)(base + off) = v; }

static void delay_us (u32 us) {            // the counter of the generic timer runs at a fixed frequency
  u64 freq, start, now;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (start));
  do __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (now)); while ((now - start) * 1000000 / freq < us);
}

// wait until (register & mask) == want, at most 'ms' milliseconds; 0 = it happened. The registers have different widths and must
// be read with their own width ('bytes' = 1, 2 or 4); a 32-bit read of the 8-bit reset register would be a misaligned access.
static int wait_for (int off, int bytes, u32 mask, u32 want, int ms) {
  for (int i = 0; i < ms * 100; i++) {
    u32 value = bytes == 1 ? rd8 (off) : bytes == 2 ? rd16 (off) : rd32 (off);
    if ((value & mask) == want) return 0;
    delay_us (10);
  }
  return -1;
}

// SDCLK = base clock / (2 * divider); divider 0 means the base clock itself. The divider has 10 bits, split over the register.
static int set_clock (u32 hz) {
  u32 basemhz = (rd32 (R_CAPS) >> 8) & 0xff;
  u32 basehz = (basemhz ? basemhz : 100) * 1000000;       // (a controller that does not say: the Pi's 100 MHz)
  u32 div = 1;
  while (div < 1023 && basehz / (2 * div) > hz) div++;
  wr16 (R_CLKCTL, 0);                                      // stop the clock while it is changed
  wr16 (R_CLKCTL, ((div & 0xff) << 8) | (((div >> 8) & 3) << 6) | 0x0001);       // divider, internal clock on
  if (wait_for (R_CLKCTL, 2, 0x0002, 0x0002, 100) != 0) return -1;                  // ... and stable
  wr16 (R_CLKCTL, rd16 (R_CLKCTL) | 0x0004);               // the clock goes to the card
  return 0;
}

static void reset_lines (int what) {                       // after an error: forget the command and the data that were under way
  wr8 (R_RESET, what);
  wait_for (R_RESET, 1, what, 0, 100);
}

// Send one command and wait for its answer. 'flags' says what kind of answer to expect (CMD_*), 'transfer' is the transfer mode
// of a data command. The answer goes to resp[0..3]. Returns 0 if all went well.
static int command (u32 index, u32 arg, u32 flags, u32 transfer, u32 *resp) {
  u32 busy = PRESENT_CMD_INHIBIT;                          // the controller must be idle before a new command
  if ((flags & CMD_DATA) || (flags & 3) == CMD_R1B) busy |= PRESENT_DAT_INHIBIT;
  if (wait_for (R_PRESENT, 4, busy, 0, 100) != 0) return -1;
  wr32 (R_INT_STATUS, 0xffffffff);                         // (writing a 1 clears a status bit)
  wr32 (R_ARG, arg);
  wr16 (R_TRNMOD, transfer);
  wr16 (R_CMD, (index << 8) | flags);
  for (int i = 0; ; i++) {
    u32 status = rd32 (R_INT_STATUS);
    if (status & INT_ERROR) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
    if (status & INT_CMD_DONE) break;
    if (i > 100000) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
    delay_us (10);
  }
  wr32 (R_INT_STATUS, INT_CMD_DONE);
  if (resp) for (int i = 0; i < 4; i++) resp[i] = rd32 (R_RESP + 4 * i);
  return 0;
}

#define GO_IDLE_STATE     0
#define ALL_SEND_CID      2
#define SEND_RELATIVE_ADDR 3
#define SELECT_CARD       7
#define SEND_IF_COND      8
#define SET_BLOCKLEN      16
#define READ_SINGLE_BLOCK 17
#define WRITE_SINGLE_BLOCK 24
#define APP_CMD           55
#define SD_SEND_OP_COND   41      // an "application command": CMD55 first

static int app_command (u32 index, u32 arg, u32 flags, u32 *resp) {
  u32 r[4];
  if (command (APP_CMD, rca << 16, CMD_R1 | CMD_CRC_CHECK | CMD_INDEX_CHECK, 0, r) != 0) return -1;
  return command (index, arg, flags, 0, resp);
}

// Bring the card from "just powered" to "ready to read and write sectors". Returns 0 on success.
static int card_init (void) {
  u32 r[4];
  wr8 (R_RESET, RESET_ALL);
  if (wait_for (R_RESET, 1, RESET_ALL, 0, 100) != 0) return -1;
  if (!(rd32 (R_PRESENT) & PRESENT_CARD)) { printf ("sd: no card in the slot\n"); return -1; }
  wr8 (R_PWRCTL, 0x0e | 0x01);                             // 3.3 V, power on
  wr8 (R_TIMEOUT, 0x0e);                                   // the longest data timeout
  wr32 (R_INT_ENABLE, 0xffffffff);                         // we poll the status, so every bit must show up there
  wr32 (R_SIG_ENABLE, 0);                                  // ... but nothing interrupts
  if (set_clock (400000) != 0) return -1;                  // a card that is not initialised yet must be slow (<= 400 kHz)
  delay_us (2000);

  rca = 0;
  if (command (GO_IDLE_STATE, 0, CMD_NO_RESPONSE, 0, 0) != 0) return -1;
  int v2 = command (SEND_IF_COND, 0x1aa, CMD_R1 | CMD_CRC_CHECK | CMD_INDEX_CHECK, 0, r) == 0 && (r[0] & 0xfff) == 0x1aa;
  u32 ocr = 0;
  for (int i = 0; i < 1000; i++) {                         // the card says "ready" in bit 31 once it has started up
    if (app_command (SD_SEND_OP_COND, 0x00ff8000 | (v2 ? 0x40000000 : 0), CMD_R1, r) != 0) return -1;
    ocr = r[0];
    if (ocr & 0x80000000) break;
    delay_us (1000);
  }
  if (!(ocr & 0x80000000)) { printf ("sd: card does not become ready\n"); return -1; }
  high_capacity = (ocr & 0x40000000) != 0;                 // bit 30: the card is addressed by sector number

  if (command (ALL_SEND_CID, 0, CMD_R2 | CMD_CRC_CHECK, 0, r) != 0) return -1;
  if (command (SEND_RELATIVE_ADDR, 0, CMD_R1 | CMD_CRC_CHECK | CMD_INDEX_CHECK, 0, r) != 0) return -1;
  rca = r[0] >> 16;
  if (command (SELECT_CARD, rca << 16, CMD_R1B | CMD_CRC_CHECK | CMD_INDEX_CHECK, 0, r) != 0) return -1;
  if (!high_capacity && command (SET_BLOCKLEN, 512, CMD_R1 | CMD_CRC_CHECK | CMD_INDEX_CHECK, 0, r) != 0) return -1;
  if (set_clock (25000000) != 0) return -1;                // now the normal speed (1 data line; 4 lines would be faster)
  printf ("sd: card ready (%s, address %u)\n", high_capacity ? "SDHC/SDXC" : "SDSC", rca);
  return 0;
}

// read or write one sector
static int sector_io (int write, u64 lba, void *buf) {
  u32 *words = buf;
  u32 arg = high_capacity ? (u32)lba : (u32)(lba * 512);
  wr16 (R_BLKSIZE, 512);
  wr16 (R_BLKCNT, 1);
  u32 ready = write ? INT_WRITE_READY : INT_READ_READY;
  u32 flags = CMD_R1 | CMD_CRC_CHECK | CMD_INDEX_CHECK | CMD_DATA;
  if (command (write ? WRITE_SINGLE_BLOCK : READ_SINGLE_BLOCK, arg, flags, TRN_BLKCNT_EN | (write ? 0 : TRN_READ), 0) != 0) return -1;
  for (int i = 0; !(rd32 (R_INT_STATUS) & (ready | INT_ERROR)); i++)
    if (i > 100000) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
  if (rd32 (R_INT_STATUS) & INT_ERROR) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
  wr32 (R_INT_STATUS, ready);
  for (int i = 0; i < 128; i++) {                          // 512 bytes through the data port, a word at a time
    if (write) wr32 (R_DATA, words[i]);
    else       words[i] = rd32 (R_DATA);
  }
  for (int i = 0; !(rd32 (R_INT_STATUS) & (INT_XFER_DONE | INT_ERROR)); i++)
    if (i > 1000000) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
  if (rd32 (R_INT_STATUS) & INT_ERROR) { reset_lines (RESET_CMD | RESET_DAT); return -1; }
  wr32 (R_INT_STATUS, INT_XFER_DONE);
  return 0;
}

// ---- what the block layer (virtio.c) uses ----

int sd_read (u64 lba, void *buf, u32 count) {
  for (u32 i = 0; i < count; i++) if (sector_io (0, lba + i, (u8 *)buf + 512 * i) != 0) return -1;
  return 0;
}

int sd_write (u64 lba, const void *buf, u32 count) {
  for (u32 i = 0; i < count; i++) if (sector_io (1, lba + i, (u8 *)buf + 512 * i) != 0) return -1;
  return 0;
}

// Initialise the card and look into the partition table. For every Ulix partition (type 0x81, at most 'max') the first sector
// and the size in sectors are stored; the result is the number of partitions found (0: no card or no Ulix partition).
int sd_partitions (u64 *first, u64 *sectors, int max) {
  if (!plat.sdhci) return 0;
  base = KERNEL_VA + plat.sdhci;
  if (card_init () != 0) { printf ("sd: card not usable\n"); return 0; }
  static u8 mbr[512] __attribute__ ((aligned (16)));
  if (sd_read (0, mbr, 1) != 0) { printf ("sd: cannot read sector 0\n"); return 0; }
  if (mbr[510] != 0x55 || mbr[511] != 0xaa) { printf ("sd: no partition table\n"); return 0; }
  int n = 0;
  for (int i = 0; i < 4 && n < max; i++) {
    u8 *e = mbr + 446 + 16 * i;                            // 16 bytes per entry: type at 4, first sector at 8, size at 12
    u32 start = e[8] | (e[9] << 8) | (e[10] << 16) | ((u32)e[11] << 24);
    u32 size  = e[12] | (e[13] << 8) | (e[14] << 16) | ((u32)e[15] << 24);
    printf ("sd: partition %d: type 0x%x, first sector %u, %u sectors\n", i + 1, e[4], start, size);
    if (e[4] == 0x81 && size > 0) { first[n] = start; sectors[n] = size; n++; }
  }
  return n;
}
