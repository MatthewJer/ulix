// Ulix ARM64 -- virtual terminals (ulix-book.nw: "Virtual Terminals", "Terminal Output")
//
// The book keeps for every terminal a screen of 80 x 24 characters, each a 16-bit word (ASCII value and VGA
// color attribute), plus a cursor position, and copies the screen of the visible terminal to the VGA text
// memory.  The same data structures (vt[], term_buffer) are used here.  The "screen" is the serial console,
// a terminal emulator that understands ANSI escape sequences, so
//   - kputch () updates the model of the terminal it writes to (cursor, wrapping, scrolling: as in the book)
//     and sends the character on if that terminal is the visible one;
//   - set_xy (), clrscr (), write_screen () and switching terminals repaint the visible terminal from its
//     model with cursor positioning and color sequences ("synchronized" mode: the console has a scroll
//     region of the 24 lines, so that scrolling there is the same as in the model).
// Until a program uses one of those functions the console just receives the characters.
// The status line (line 25) does not exist.

#define hash arch_vt_hash                          // the book defines hash() in the header: keep this copy private
#include "ulix-api.h"

extern void putch (char c);
void vt_scroll_mem (word *address);                // the book defines it without a prototype

#define BLANK       (0x20 | VT_NORMAL_BACKGROUND)
static int vt_ready = 0;
static int synced = 0;                             // the console shows the model of the visible terminal exactly

static void vt_setup (void) {
  for (int t = 0; t <= MAX_VT; t++) {
    memsetw ((word *)vt[t].mem, BLANK, VT_SIZE / 2);
    vt[t].x = vt[t].y = 0;
  }
  vt_ready = 1;
}

// ---- the console ----
static void esc (const char *s) { while (*s) putch (*s++); }

static void num (int n) {
  char b[12]; int i = 0;
  if (n == 0) b[i++] = '0';
  while (n > 0) { b[i++] = '0' + n % 10; n /= 10; }
  while (i > 0) putch (b[--i]);
}

static void host_move (int x, int y) {             // cursor to column x, line y (both from 0)
  esc ("\033["); num (y + 1); putch (';'); num (x + 1); putch ('H');
}

static void host_attr (int attr) {                 // VGA attribute -> ANSI colors
  static const char ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };      // VGA color number -> ANSI color number
  if ((attr & 0xff) == 0x0F || (attr & 0xff) == 0) { esc ("\033[0m"); return; }
  int fg = attr & 15, bg = (attr >> 4) & 7;
  esc ("\033[0;"); num ((fg < 8 ? 30 : 90) + ansi[fg & 7]); putch (';'); num (40 + ansi[bg]); putch ('m');
}

// paint the whole model of terminal t on the console
static void host_repaint (int t) {
  word *mem = (word *)vt[t].mem;
  if (!synced) esc ("\033[2J\033[1;24r");          // first time: clear, scroll region = the 24 lines of the terminal
  for (int row = 0; row < VT_HEIGHT; row++) {
    int last = -1;
    for (int col = 0; col < VT_WIDTH; col++) {
      word w = mem[row * VT_WIDTH + col];
      if ((w & 0xff) > ' ' || (w >> 8 & 0xff) != 0x0F) last = col;
    }
    if (row == vt[t].y && last < vt[t].x - 1) last = vt[t].x - 1;     // text up to the cursor (a prompt ends with a blank)
    host_move (0, row);
    int cur_attr = -1;
    for (int col = 0; col <= last; col++) {
      word w = mem[row * VT_WIDTH + col];
      int attr = w >> 8, ch = w & 0xff;
      if (ch < ' ') ch = ' ';
      if (attr != cur_attr) { host_attr (attr); cur_attr = attr; }
      putch (ch);
    }
    esc ("\033[0m\033[K");                         // the rest of the line is empty
  }
  host_move (vt[t].x, vt[t].y);
  synced = 1;
}

// ---- output ----
void kputch (byte c) {
  if (!vt_ready) vt_setup ();
  // a process writes to its terminal; the kernel (no scheduler yet, kernel shell) writes to the visible one
  int t = scheduler_is_active ? thread_table[current_task].terminal : cur_vt;
  if (t < 0 || t > MAX_VT) t = cur_vt;
  term_buffer *term = &vt[t];
  int wrapped = 0;

  switch (c) {
    case '\b': if (term->x != 0) term->x--; break;
    case '\r': term->x = 0; break;
    case '\n': term->x = 0; term->y++; break;
  }
  if (c >= ' ') {                                  // normal character
    ((word *)term->mem)[term->y * VT_WIDTH + term->x] = c | VT_NORMAL_BACKGROUND;
    term->x++;
  }
  if (term->x >= VT_WIDTH) { term->x = 0; term->y++; wrapped = 1; }     // end of line reached
  if (term->y >= VT_HEIGHT) {                      // scroll if necessary
    vt_scroll_mem ((word *)term->mem);
    term->y = VT_HEIGHT - 1;
  }

  if (t == cur_vt) {                               // the visible terminal: the console shows it, too
    putch ((char)c);
    if (wrapped) putch ('\n');                     // (putch sends CR LF)
  }
}

// ---- terminal functions of the book ----
static int process_terminal (void) {
  int t = scheduler_is_active ? thread_table[current_task].terminal : cur_vt;
  return (t < 0 || t > MAX_VT) ? cur_vt : t;
}

void vt_clrscr (void) {
  if (!vt_ready) vt_setup ();
  int t = process_terminal ();
  memsetw ((word *)vt[t].mem, BLANK, VT_SIZE / 2);
  vt[t].x = vt[t].y = 0;
  if (t == cur_vt) host_repaint (t);
}

void vt_get_xy (char *x, char *y) {
  if (!vt_ready) vt_setup ();
  int t = process_terminal ();
  *x = vt[t].x;
  *y = vt[t].y;
}

void vt_set_xy (char x, char y) {
  if (!vt_ready) vt_setup ();
  int t = process_terminal ();
  if (x < 0 || x >= VT_WIDTH) x = VT_WIDTH - 1;    // (the book does not check)
  if (y < 0 || y >= VT_HEIGHT) y = VT_HEIGHT - 1;
  vt[t].x = x;
  vt[t].y = y;
  if (t == cur_vt) { if (synced) host_move (x, y); else host_repaint (t); }
}

void read_write_screen (char *buf, boolean read_flag) {
  if (!vt_ready) vt_setup ();
  int t = process_terminal ();
  char *video_address = (char *)vt[t].mem;
  if (read_flag) {
    memcpy (buf, video_address, VT_SIZE);          // read the screen
  } else {
    memcpy (video_address, buf, VT_SIZE);          // write the screen
    if (t == cur_vt) host_repaint (t);
  }
}

// make terminal n the visible one: the console shows its screen (the x86 copies it to the video memory)
void arch_vt_switch (int n) {
  if (!vt_ready) vt_setup ();
  if (n < 0 || n > MAX_VT || n == cur_vt) return;
  cur_vt = n;
  host_repaint (n);
}

// ---- /dev/kmem ----
// On the x86 the device is the physical memory, from address 0, and the VGA text memory sits in it at 0xb8000 (80 x 25
// characters of 2 bytes). Here byte 0 is the first byte of RAM, and the window at 0xb8000 is the screen of the visible
// terminal (24 lines; the 25th line, the status line of the x86, reads as blanks and ignores writes).
extern unsigned long arch_ram_base (void), arch_ram_size (void);
#define VGA_BASE   0xb8000UL
#define VGA_BYTES  (80 * 25 * 2)

static void kmem_copy_ram (char *buf, unsigned long off, int n, int write) {
  unsigned long base = arch_ram_base (), size = arch_ram_size ();
  if (off >= size) { if (!write) memset (buf, 0, n); return; }
  if (off + n > size) { if (!write) memset (buf + (size - off), 0, off + n - size); n = size - off; }
  if (write) memcpy ((char *)PHYSICAL (base + off), buf, n);
  else       memcpy (buf, (char *)PHYSICAL (base + off), n);
}

static void kmem_access (char *buf, unsigned long off, int n, int write) {
  if (!vt_ready) vt_setup ();
  while (n > 0) {
    unsigned long end = off + n;
    if (off < VGA_BASE) {                                  // RAM below the window
      int part = end > VGA_BASE ? (int)(VGA_BASE - off) : n;
      kmem_copy_ram (buf, off, part, write);
      off += part; buf += part; n -= part;
    } else if (off < VGA_BASE + VGA_BYTES) {               // the screen
      int part = end > VGA_BASE + VGA_BYTES ? (int)(VGA_BASE + VGA_BYTES - off) : n;
      for (int i = 0; i < part; i++) {
        unsigned long idx = off - VGA_BASE + i;
        if (idx < VT_SIZE) { if (write) vt[cur_vt].mem[idx] = buf[i]; else buf[i] = vt[cur_vt].mem[idx]; }
        else if (!write) buf[i] = (idx & 1) ? 0x1f : ' ';
      }
      if (write) host_repaint (cur_vt);
      off += part; buf += part; n -= part;
    } else {                                               // RAM above it
      kmem_copy_ram (buf, off, n, write);
      return;
    }
  }
}

void arch_kmem_read (char *buf, unsigned long offset, int n)  { kmem_access (buf, offset, n, 0); }
void arch_kmem_write (unsigned long offset, char *buf, int n) { kmem_access (buf, offset, n, 1); }
