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
void buffer_sync (boolean lock_buffer) { }                                             // nothing is cached
boolean buffer_contains (int dev, int blockno)                           { return 0; }

// ---- console output: the book's kputch() writes to the VGA text screen; here it is the UART ----
extern void putch (char c);

// There is one physical console (the UART), but the book has ten virtual terminals. Output of a process
// whose terminal is not the visible one is kept in a buffer per terminal and shown when the user
// switches to it (Ctrl-T followed by 1..9 or 0; on the x86 this is Alt-1..0 and the screen contents are kept).
#define VT_BUF 4096
static char vt_out[10][VT_BUF];
static int  vt_len[10];

void kputch (byte c) {
  if (scheduler_is_active && current_task > 1) {
    int t = thread_table[current_task].terminal;
    if (t != cur_vt && t >= 0 && t < 10) {
      if (vt_len[t] == VT_BUF) {                         // full: drop the oldest half
        for (int i = 0; i < VT_BUF / 2; i++) vt_out[t][i] = vt_out[t][i + VT_BUF / 2];
        vt_len[t] = VT_BUF / 2;
      }
      vt_out[t][vt_len[t]++] = c;
      return;
    }
  }
  putch ((char)c);
}

// make terminal n the visible one and show what its processes have written meanwhile
void arch_vt_switch (int n) {
  if (n < 0 || n > 9 || n == cur_vt) return;
  cur_vt = n;
  printf ("\n[terminal %d]\n", n + 1);
  for (int i = 0; i < vt_len[n]; i++) putch (vt_out[n][i]);
  vt_len[n] = 0;
}

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

void vt_clrscr (void)        { printf ("\033[2J\033[H"); }              // ANSI: clear the terminal
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
