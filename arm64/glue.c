// Ulix ARM64 -- platform glue between the kernel code taken from ulix-book.nw (ulix-port.c)
// and the ARM64 drivers.  Everything here replaces something that is hardware specific in
// the x86 version of the kernel.

#include "ulix-api.h"          // the book's types, constants and prototypes (generated)

extern int  blk_read  (unsigned long sector, void *buf, unsigned nsectors);
extern int  blk_write (unsigned long sector, void *buf, unsigned nsectors);
extern unsigned long blk_capacity (void);

// ---- block devices: the book's readblock() calls these two for /dev/hda, /dev/hdb ----
// A block is 1 KByte = 2 sectors of 512 bytes (HD_SECSIZE).  The only disk is the virtio disk.
void readblock_hd (int hd, int blockno, char *buffer) {
  if (hd != 0) return;
  blk_read ((unsigned long)blockno * 2, buffer, 2);
}

void writeblock_hd (int hd, int blockno, char *buffer) {
  if (hd != 0) return;
  blk_write ((unsigned long)blockno * 2, buffer, 2);
}

// There is no floppy controller and no serial disk on this platform.
void readblock_fd  (int device, int blockno, char *buffer) { }
void writeblock_fd (int device, int blockno, char *buffer) { }
void readblock_serial  (int secno, char *buf) { }
void writeblock_serial (int secno, char *buf) { }

// Size of a device in bytes (the book's version asks the floppy and ATA drivers).
long dev_size (int dev) {
  if (dev == DEV_HDA) return (long)(blk_capacity () * 512);
  return 0;
}

// ---- writeblock(): the book only stores the block in its buffer cache and lets a flusher thread
// write it to disk later.  Until the cache (it needs the thread table and mutexes) is ported,
// every block is written through to the disk immediately.
void writeblock_raw (int device, int blockno, char *buffer, char flag);   // from the book

void writeblock (int device, int blockno, char *buffer) {
  writeblock_raw (device, blockno, buffer, DONT_UPDATE_BUF);
}

// ---- buffer cache: not ported yet (it needs the thread table and mutexes of the book) ----
int buffer_read (int dev, int blockno, char *block)                      { return -1; }  // "not cached"
int buffer_write (int dev, int blockno, char *block, char dirtyflag)     { return 0; }
boolean buffer_contains (int dev, int blockno)                           { return 0; }

// ---- console output: the book's kputch() writes to the VGA text screen; here it is the UART ----
extern void putch (char c);
void kputch (byte c) { putch ((char)c); }

// ---- diagnostics ----
int debug_printf (const char *format, ...) { return 0; }                 // debugging output off

// ---- console input for the book's u_read(): one character from the PL011 UART ----
#define UART_BASE   (0xffff000000000000UL + 0x09000000UL)
unsigned int arch_readchar (void) {
  volatile unsigned int *fr = (volatile unsigned int *)(UART_BASE + 0x18);
  volatile unsigned int *dr = (volatile unsigned int *)(UART_BASE + 0x00);
  while (*fr & (1 << 4)) __asm__ volatile ("wfe");       // RXFE: receive FIFO empty
  return *dr & 0xff;
}
