// Ulix ARM64 -- the console: keyboard input comes from the PL011 UART instead of the PS/2 keyboard.
//
// On the x86 the keyboard interrupt handler turns scan codes into characters and stores them in the
// input buffer of the current virtual terminal (terminals[cur_vt]); processes that wait for a key sit
// in keyboard_queue.  Here the UART receive interrupt does the same job; everything behind the
// buffer (kgetch, syscall_readchar, kreadline, the kernel shell) is the code from the book.
//
//   Enter          -> '\n'                (terminals send CR)
//   Backspace/DEL  -> '\b'
//   Ctrl-C         -> kill the foreground process of the terminal (like the x86 keyboard handler)
//   Ctrl-]         -> stop the scheduler and enter the kernel shell (x86: Shift-Esc)

#include "ulix-api.h"

extern unsigned long uart_base;
#define UART_BASE   (0xffff000000000000UL + uart_base)
#define UART_DR     (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_FR     (*(volatile unsigned int *)(UART_BASE + 0x18))
#define UART_IMSC   (*(volatile unsigned int *)(UART_BASE + 0x38))
#define UART_ICR    (*(volatile unsigned int *)(UART_BASE + 0x44))
#define UART_FR_RXFE (1 << 4)

void kernel_shell (void);
extern void irq_uart_to_cpu (int cpu);
extern int smp_cpu (void);

#include "ulix-init.c"                     // arch_setup_keyboard(): the book's <<setup keyboard>> chunk

void uart_rx_enable (void) {
  arch_setup_keyboard ();                   // empty input buffers (kbd_lastread = -1, ...)
  UART_ICR  = 0x7ff;
  UART_IMSC = (1 << 4) | (1 << 6);          // receive interrupt and receive timeout interrupt
}

// the character is complete (already translated): put it where the book's keyboard handler puts it
static void console_enqueue (char c) {
  terminal_t *term = &terminals[cur_vt];
  if (term->kbd_count >= SYSTEM_KBD_BUFLEN) return;
  term->kbd[term->kbd_pos] = c;
  term->kbd_pos = (term->kbd_pos + 1) % SYSTEM_KBD_BUFLEN;
  term->kbd_count++;
  if (scheduler_is_active) {                // wake up a process that waits for a key on this terminal
    thread_id start_pid = keyboard_queue.next;
    if (start_pid != 0) {
      thread_id search_pid = start_pid;
      do {
        if (thread_table[search_pid].terminal == cur_vt) {
          deblock (search_pid, &keyboard_queue);
          break;
        } else {
          search_pid = thread_table[search_pid].next;
        }
      } while (search_pid != start_pid && search_pid != 0);
    }
  }
}

// Ctrl-C: the last process of the terminal that has no children (the foreground process)
static void console_interrupt (void) {
  int target_pid = 0;
  for (int i = 3; i < MAX_THREADS; i++) {
    if (thread_table[i].used && thread_table[i].terminal == cur_vt) {
      int is_candidate = true;
      for (int j = 3; j < MAX_THREADS; j++) {
        if (thread_table[j].used && thread_table[j].ppid == i) { is_candidate = false; break; }
      }
      if (is_candidate) { target_pid = i; break; }
    }
  }
  u_kill (target_pid, SIGKILL);
}

void arch_vt_switch (int n);

void arch_console_input (unsigned char c) {
  static int vt_prefix = 0;                  // after Ctrl-T the next digit selects a terminal
  if (vt_prefix) {
    vt_prefix = 0;
    if (c >= '0' && c <= '9') { arch_vt_switch ((c - '0' + 9) % 10); return; }   // like Alt-1..Alt-0 of the book
  }
  if (c == 0x14) { vt_prefix = 1; return; }                                       // Ctrl-T
  if (c == '\r') c = '\n';
  if (c == 0x7f) c = 0x08;
  if (c == 0x03) { console_interrupt (); return; }
  if (c == 0x1d) {                          // Ctrl-]
    if (scheduler_is_active) {
      scheduler_is_active = false;
      printf ("\nGoing to kernel shell\n");
      irq_uart_to_cpu (smp_cpu ());
      kernel_shell ();
      irq_uart_to_cpu (0);
      printf ("returning from kernel shell\n");
    }
    return;
  }
  console_enqueue ((char)c);
}

// called by the interrupt handler
void uart_rx_irq (void) {
  while (!(UART_FR & UART_FR_RXFE)) {
    unsigned char c = UART_DR & 0xff;
    UART_ICR = 0x7ff;
    arch_console_input (c);
  }
  UART_ICR = 0x7ff;
}

// An exception inside the kernel cannot be fixed (for example a bad user pointer in a system call).  Like the
// page fault handler of the book: stop the scheduler, enable interrupts and jump to the kernel shell, which
// is never left again.
void arch_kernel_fault_shell (void) {
  scheduler_is_active = false;
  __asm__ volatile ("msr daifclr, #2");
  printf ("\n");
  irq_uart_to_cpu (smp_cpu ());                  // the keys must come to this CPU
  kernel_shell ();
  for (;;) __asm__ volatile ("wfe");
}
