// Ulix ARM64 -- platform glue between the kernel code taken from ulix-book.nw (ulix-port.c)
// and the ARM64 drivers.  Everything here replaces something that is hardware specific in
// the x86 version of the kernel.

#include "ulix-api.h"          // the book's types, constants and prototypes (generated)

extern int  blk_read  (unsigned long sector, void *buf, unsigned nsectors);
extern int  blk_write (unsigned long sector, void *buf, unsigned nsectors);
extern unsigned long blk_capacity (void);

// ---- block devices: the book's readblock() calls these two for /dev/hda, /dev/hdb ----
// A block is 1 KByte = 2 sectors of 512 bytes (HD_SECSIZE).  The disks are the virtio disks (up to two).
// Like the IDE driver of the book, a process that reads or writes a block takes hd_lock, starts the transfer and goes
// to sleep in harddisk_queue; the interrupt of the device wakes it up (hd_interrupt).  Without a running scheduler,
// for the idle thread (the tests, the boot code) and without a known interrupt the driver polls instead.
extern int  blk_submit_n (int n, int type, unsigned long sector, void *buf, unsigned nsectors);
extern int  blk_done_n (int n);
extern int  blk_finish_n (int n);
extern int  blk_disks;
extern unsigned long blk_capacity_of (int n);
extern void blk_irq_ack (void);
extern int  blk_irq;
extern unsigned long blk_capacity (void);
void arch_resign (void);

int hd_sleeps = 0, hd_interrupts = 0;        // statistics, checked by the tests

static int hd_transfer (int hd, int type, int blockno, char *buffer) {
  int sleep_ok = scheduler_is_active && blk_irq && current_task > 1;       // (thread 1 is the idle thread and must not sleep)
  if (hd_lock == NULL) hd_lock = get_new_lock ("hd");
  if (current_task > 1) mutex_lock (hd_lock);
    hd_direction = (type == 0) ? HD_OP_READ : HD_OP_WRITE;
    int r = blk_submit_n (hd, type, (unsigned long)blockno * 2, buffer, 2);
    if (r == 0) {
      while (!blk_done_n (hd)) {
        if (sleep_ok) { hd_sleeps++; block (&harddisk_queue, TSTATE_WAITHD); arch_resign (); }
      }
      r = blk_finish_n (hd);
    }
    hd_direction = HD_OP_NONE;
  if (current_task > 1) mutex_unlock (hd_lock);
  return r;
}

void readblock_hd (int hd, int blockno, char *buffer) {
  if (hd < 0 || hd >= blk_disks) return;
  hd_transfer (hd, 0, blockno, buffer);
}

void writeblock_hd (int hd, int blockno, char *buffer) {
  if (hd < 0 || hd >= blk_disks) return;
  hd_transfer (hd, 1, blockno, buffer);
}

// the interrupt handler of the disk: acknowledge, wake up the process that waits for the transfer
void hd_interrupt (void) {
  blk_irq_ack ();
  hd_interrupts++;
  thread_id t = front_of_blocked_queue (harddisk_queue);
  if (t) deblock (t, &harddisk_queue);
}

// There is no floppy controller on this platform.  A third virtio disk stands in for the second floppy
// /dev/fd1 (which the book mounts on /mnt); without one, and for fd0, nothing is read or written.
void readblock_fd  (int device, int blockno, char *buffer) { if (device == 1 && blk_disks >= 3) hd_transfer (2, 0, blockno, buffer); }
void writeblock_fd (int device, int blockno, char *buffer) { if (device == 1 && blk_disks >= 3) hd_transfer (2, 1, blockno, buffer); }

// ---- the serial disk (/dev/sdisk, major 42): a disk on the second PL011 UART, protocol of the book ----
// A request is a command byte (CMD_GET or CMD_PUT) and the block number as four bytes, lowest byte first; CMD_PUT is followed by
// the 1024 bytes of the block, CMD_GET is answered with 1024 bytes.  The other end is a program on the host that serves a disk
// image (tools/serial_disk.py).  Without a second UART in the device tree there is no serial disk: nothing is read or written.
// A process that reads sleeps in serial_disk_queue until the receive interrupt has collected the block; the idle thread (tests,
// boot code) polls instead.  As in qemu the baud rate does not matter, it is not programmed; the port is only switched on.
extern unsigned long plat_uart2 (void);
extern int  plat_uart2_irq (void);
#define SD_REG(off)  (*(volatile unsigned int *)(0xffff000000000000UL + plat_uart2 () + (off)))
#define SD_DR 0x00
#define SD_FR 0x18
#define SD_LCRH 0x2c
#define SD_CR 0x30
#define SD_IMSC 0x38
#define SD_ICR 0x44
#define SD_FR_RXFE (1 << 4)
#define SD_FR_TXFF (1 << 5)

static char sd_buf[BLOCK_SIZE];
static volatile int sd_pos = 0;
static int sd_irq_ready = 0;
static lock sd_lock = NULL;
int sd_blocks_read = 0, sd_blocks_written = 0, sd_sleeps = 0;       // statistics, checked by the tests

void sdisk_init (void) {
  if (!plat_uart2 ()) return;
  SD_REG (SD_CR) = 0;
  SD_REG (SD_LCRH) = 0x60;                       // 8 bits, no parity, FIFOs off: every received byte raises an interrupt
                                                 // (qemu's PL011 has no receive timeout, so bytes below the FIFO trigger level would wait forever)
  SD_REG (SD_CR) = 0x301;                        // UART, transmitter and receiver on
  SD_REG (SD_IMSC) = 0;                          // the receive interrupt is only on while a process sleeps
  SD_REG (SD_ICR) = 0x7ff;
  sd_irq_ready = plat_uart2_irq () != 0;
}

static void sd_putc (unsigned char c) {
  while (SD_REG (SD_FR) & SD_FR_TXFF) ;
  SD_REG (SD_DR) = c;
}

static void sd_command (unsigned char cmd, unsigned int secno) {
  sd_putc (cmd);
  for (int i = 0; i < 4; i++) { sd_putc (secno & 0xff); secno >>= 8; }
}

// the interrupt handler: move the bytes of the FIFO into the block buffer; when it is complete, wake the sleeping process
void sdisk_irq (void) {
  SD_REG (SD_ICR) = 0x7ff;                       // first clear (a byte that arrives while we read must raise the interrupt again), then read
  while (!(SD_REG (SD_FR) & SD_FR_RXFE)) {
    char c = (char)(SD_REG (SD_DR) & 0xff);
    if (sd_pos < BLOCK_SIZE) sd_buf[sd_pos++] = c;
  }
  if (sd_pos == BLOCK_SIZE) {
    SD_REG (SD_IMSC) = 0;                        // no more interrupts until the next request
    thread_id t = front_of_blocked_queue (serial_disk_queue);
    if (t) deblock (t, &serial_disk_queue);
  }
}

static void sd_transfer (int write, int secno, char *buf) {
  if (!plat_uart2 ()) return;
  int sleep_ok = scheduler_is_active && sd_irq_ready && current_task > 1;
  if (sd_lock == NULL) sd_lock = get_new_lock ("serial disk");
  if (current_task > 1) mutex_lock (sd_lock);
    if (write) {
      sd_command (CMD_PUT, secno);
      for (int i = 0; i < BLOCK_SIZE; i++) sd_putc ((unsigned char)buf[i]);
      sd_blocks_written++;
    } else {
      while (!(SD_REG (SD_FR) & SD_FR_RXFE)) (void)SD_REG (SD_DR);     // old bytes of an earlier, failed request
      sd_pos = 0;
      sd_command (CMD_GET, secno);
      if (sleep_ok) {
        SD_REG (SD_IMSC) = (1 << 4) | (1 << 6);                      // receive and receive timeout interrupt
        while (sd_pos < BLOCK_SIZE) { sd_sleeps++; block (&serial_disk_queue, TSTATE_WAITSD); arch_resign (); }
      } else {
        long spins = 0;
        while (sd_pos < BLOCK_SIZE && spins < 400000000L) {
          if (!(SD_REG (SD_FR) & SD_FR_RXFE)) { sd_buf[sd_pos++] = (char)(SD_REG (SD_DR) & 0xff); spins = 0; } else spins++;
        }
        if (sd_pos < BLOCK_SIZE) printf ("serial disk: no answer for block %d\n", secno);
      }
      if (sd_pos == BLOCK_SIZE) { memcpy (buf, sd_buf, BLOCK_SIZE); sd_blocks_read++; }
    }
  if (current_task > 1) mutex_unlock (sd_lock);
}

void readblock_serial  (int secno, char *buf) { sd_transfer (0, secno, buf); }
void writeblock_serial (int secno, char *buf) { sd_transfer (1, secno, buf); }

// Size of a device in bytes (the book's version asks the floppy and ATA drivers).
long dev_size (int dev) {
  if (dev == DEV_HDA) return (long)(blk_capacity_of (0) * 512);
  if (dev == DEV_HDB) return (long)(blk_capacity_of (1) * 512);
  if (dev == DEV_FD1) return (long)(blk_capacity_of (2) * 512);
  return 0;
}

// The buffer cache (buffer_read, buffer_write, buffer_sync, writeblock, ...) is the code of the book.
// Blocks are written to the disk by buffer_sync (the sync system call, or when the cache is full).

// Console output (kputch), the terminal functions (vt_clrscr, set_xy, ...) and terminal switching are in arch_vt.c.

// ---- the status line (arch_vt.c) ----

// ---- diagnostics ----
int debug_printf (const char *format, ...) { return 0; }                 // debugging output off

// ---- console input for the book's u_read(): the readchar system call, called from the kernel ----
// (on the x86 u_read() executes int 0x80 with system call 525; syscall_readchar blocks the
// process in keyboard_queue until the UART interrupt delivers a character)
void syscall_readchar (context_t *r);
unsigned int arch_readchar (void) {
  context_t r;
  memset (&r, 0, sizeof r);
  syscall_readchar (&r);
  return (unsigned int)r.ebx;
}

// =====================================================================================
// Kernel shell: the book's kernel_shell() and ksh_run_command() are used unchanged; the commands
// that are about x86 hardware (VGA status line, 2-level page tables, the x86 frame table, ...)
// have these replacements.
// =====================================================================================
extern void putch (char c);
extern unsigned long alloc_frame (void);
extern void free_frame (unsigned long pa);
extern long frames_in_use;


void arch_ksh_test (void);
void ksh_command_test (void)  { arch_ksh_test (); }
// An integer division by zero does not trap on AArch64 (the result is 0), so the fault that the x86 command
// provokes is raised by hand (a breakpoint instruction); like on the x86 it ends in the kernel shell.
void ksh_command_div0 (void)  { printf ("div0: AArch64 divides by zero without a fault; raising the exception by hand\n"); __asm__ volatile ("brk #0"); }
void arch_dump_as (addr_space_id as);
void ksh_command_mem (void)   { printf ("frames in use: %ld (%ld KByte)\n", frames_in_use, frames_in_use * 4); }
void ksh_print_page_table (void) { arch_dump_as (current_as); }      // 4-level tables of the current address space
void ksh_command_hexdump (void) { hexdump ((memaddress)ksh_command_hexdump, (memaddress)ksh_command_hexdump + 128); }

uint request_new_frame (void) {
  frames_in_use += 0;                                                     // (the allocator counts the frame itself)
  return (uint)(alloc_frame () >> 12);                                    // frame number = physical address / 4096
}
void *request_new_page (void)  { return (void *)PHYSICAL (alloc_frame ()); }
void *request_new_pages (int n) { void *p = 0; for (int i = 0; i < n; i++) p = request_new_page (); return p; }
