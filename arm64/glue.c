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

void ksh_command_test (void)  { printf ("test: not available on AArch64\n"); }
void ksh_command_div0 (void)  { printf ("div0: an integer division by zero does not trap on AArch64\n"); }
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
void release_page_range (uint first, uint last) { printf ("rp: not available on AArch64\n"); }
