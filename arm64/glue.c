// Ulix ARM64 -- platform glue between the kernel code taken from ulix-book.nw (ulix-port.c)
// and the ARM64 drivers.  Everything here replaces something that is hardware specific in
// the x86 version of the kernel.

#include "ulix-api.h"          // the book's types, constants and prototypes (generated)

extern int  blk_read  (unsigned long sector, void *buf, unsigned nsectors);
extern int  blk_write (unsigned long sector, void *buf, unsigned nsectors);
extern unsigned long blk_capacity (void);

// ---- block devices: the book's readblock() calls these two for /dev/hda, /dev/hdb ----
// A block is 1 KByte = 2 sectors of 512 bytes (HD_SECSIZE).  The only disk is the virtio disk.
// Like the IDE driver of the book, a process that reads or writes a block takes hd_lock, starts the transfer and goes
// to sleep in harddisk_queue; the interrupt of the device wakes it up (hd_interrupt).  Without a running scheduler,
// for the idle thread (the tests, the boot code) and without a known interrupt the driver polls instead.
extern int  blk_submit (int type, unsigned long sector, void *buf, unsigned nsectors);
extern int  blk_done (void);
extern int  blk_finish (void);
extern void blk_irq_ack (void);
extern int  blk_irq;
extern unsigned long blk_capacity (void);
void arch_resign (void);

int hd_sleeps = 0, hd_interrupts = 0;        // statistics, checked by the tests

static int hd_transfer (int type, int blockno, char *buffer) {
  int sleep_ok = scheduler_is_active && blk_irq && current_task > 1;       // (thread 1 is the idle thread and must not sleep)
  if (hd_lock == NULL) hd_lock = get_new_lock ("hd");
  if (current_task > 1) mutex_lock (hd_lock);
    hd_direction = (type == 0) ? HD_OP_READ : HD_OP_WRITE;
    int r = blk_submit (type, (unsigned long)blockno * 2, buffer, 2);
    if (r == 0) {
      while (!blk_done ()) {
        if (sleep_ok) { hd_sleeps++; block (&harddisk_queue, TSTATE_WAITHD); arch_resign (); }
      }
      r = blk_finish ();
    }
    hd_direction = HD_OP_NONE;
  if (current_task > 1) mutex_unlock (hd_lock);
  return r;
}

void readblock_hd (int hd, int blockno, char *buffer) {
  if (hd != 0) return;
  hd_transfer (0, blockno, buffer);
}

void writeblock_hd (int hd, int blockno, char *buffer) {
  if (hd != 0) return;
  hd_transfer (1, blockno, buffer);
}

// the interrupt handler of the disk: acknowledge, wake up the process that waits for the transfer
void hd_interrupt (void) {
  blk_irq_ack ();
  hd_interrupts++;
  thread_id t = front_of_blocked_queue (harddisk_queue);
  if (t) deblock (t, &harddisk_queue);
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

// The buffer cache (buffer_read, buffer_write, buffer_sync, writeblock, ...) is the code of the book.
// Blocks are written to the disk by buffer_sync (the sync system call, or when the cache is full).

// Console output (kputch), the terminal functions (vt_clrscr, set_xy, ...) and terminal switching are in arch_vt.c.

// ---- the x86 status line at the bottom of the VGA screen does not exist here ----
void _set_statusline (char *text, int offset) { }

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

void statusline_red (void)   { }
void statusline_blue (void)  { }
void set_statusline (char *text) { }

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
