// Ulix ARM64 -- exceptions, GICv2, the ARM generic timer, system calls
// (replace IDT, 8259 PIC, 8253 PIT and the int 0x80 handler of the x86 version)

#include "ulix.h"
#include "plat.h"
#include "gic.h"
#include "smp.h"

extern char vectors[];
extern int  book_scheduler_on;
extern void book_timer_tick (void);
extern void book_syscall (void *frame);
extern void book_user_fault (void *frame);
extern int  book_current_pid (void);
extern void arch_signal_check (void *frame);
extern void uart_rx_irq (void);
extern void uart_rx_enable (void);
extern void arch_clock_tick (void);
extern int  arch_cow_fault (u64 va);
extern void smp_before_return (void *frame);
extern int  blk_irq;
extern void blk_irq_ack (void);
extern void hd_interrupt (void);
extern void arch_kernel_fault_shell (void);
extern int  arch_user_fault (u64 va);

// ---- the interrupt controller is in gic.c; the interrupt numbers come from the device tree ----
#define IRQ_TIMER   (plat.timer_irq)    // EL1 physical timer (a PPI; 30 on qemu)
#define IRQ_UART    (plat.uart_irq)     // PL011 UART (an SPI; 33 on qemu)

// ---- generic timer -------------------------------------------------------
#define HZ 100
static u64 timer_ticks_per_irq;
volatile u64 ticks = 0;

static void timer_reload (void) {
  __asm__ volatile ("msr cntp_tval_el0, %0" :: "r" (timer_ticks_per_irq));
}

static void timer_enable (void) {                            // for the calling CPU
  timer_reload ();
  __asm__ volatile ("msr cntp_ctl_el0, %0" :: "r" (1UL));    // enable, unmasked
  gic_enable (IRQ_TIMER);
}

static void timer_init (int hz) {
  u64 freq;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  timer_ticks_per_irq = freq / hz;
  printf ("Timer: %lu Hz counter, %d interrupts/s\n", freq, hz);
  timer_enable ();
}

void irq_init_cpu (void) {                                   // vector table and timer of a secondary CPU
  __asm__ volatile ("msr vbar_el1, %0; isb" :: "r" (vectors));
  timer_enable ();
}

// ---- exception entry -----------------------------------------------------
volatile int expect_fault = 0, faults = 0, procs_killed = 0;
volatile u64 fault_esr, fault_far;

static void exc_handler_locked (struct frame *f, int kind) {
  int from_user = (f->spsr & 0xf) == 0;     // M[3:0] == EL0t

  if (kind == 1) {                          // IRQ
    u32 iar = gic_ack ();
    u32 irq = GIC_INTID (iar);
    if (irq < 1020) gic_eoi (iar);          // acknowledge first: nested interrupts must be possible
                                            // (the kernel shell waits for keys inside the UART handler)
    if (irq == IRQ_TIMER) { if (smp_cpu () == 0) { ticks++; arch_clock_tick (); } timer_reload (); }
    else if (irq == IRQ_UART) { if (book_scheduler_on) uart_rx_irq (); }
    else if (blk_irq && irq == (u32)blk_irq) { if (book_scheduler_on) hd_interrupt (); else blk_irq_ack (); }   // the disk
    else if (irq < 16) ;                    // software interrupt from another CPU: entering the kernel was the point
    else if (irq < 1020) printf ("spurious IRQ %u\n", irq);
    if (irq == IRQ_TIMER && book_scheduler_on) book_timer_tick ();   // time slice over (10 ms)
    if (from_user && book_scheduler_on) arch_signal_check (f);       // deliver pending signals
    return;
  }

  u64 esr, far;
  __asm__ volatile ("mrs %0, esr_el1" : "=r" (esr));
  __asm__ volatile ("mrs %0, far_el1" : "=r" (far));
  u32 ec = esr >> 26;

  // write to a copy-on-write page (a permission fault on a write): from user mode, or from the kernel while
  // it writes into a user buffer during a system call. After the copy the instruction is simply repeated.
  if (kind == 0 && (ec == 0x24 || ec == 0x25) && (esr & (1 << 6)) && (esr & 0x3c) == 0x0c &&
      far < 0x80000000UL && book_scheduler_on && arch_cow_fault (far))
    return;

  // a page that was paged out (translation fault) or whose access flag was cleared for the page replacement
  // counters (access flag fault); from user mode, or from the kernel while it uses a user pointer
  if (kind == 0 && (ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) && far < 0x80000000UL && book_scheduler_on &&
      (((esr & 0x3c) == 0x04) || ((esr & 0x3c) == 0x08)) && arch_user_fault (far))
    return;

  if (kind == 0 && ec == 0x15) {            // SVC from AArch64
    if (from_user) { book_syscall (f); arch_signal_check (f); return; }       // the book's system call dispatcher
    printf ("SVC #%u at 0x%lx (from the kernel)\n", (u32)(esr & 0xffff), f->elr - 4);
    return;
  }
  if (expect_fault && !from_user && kind == 0 && ec == 0x25) {  // data abort from a kernel self-test
    fault_esr = esr; fault_far = far; faults++;
    f->elr += 4;                            // skip the faulting instruction
    return;
  }
  if (from_user && kind == 0) {             // user mistake: kill the process, not the kernel
    printf ("[kernel] process %d killed: EC=0x%x ESR=0x%lx FAR=0x%lx ELR=0x%lx\n",
            book_current_pid (), ec, esr, far, f->elr);
    procs_killed++;
    book_user_fault (f);                            // exit(-1) through the book's syscall_exit
  }
  printf ("\n*** KERNEL EXCEPTION kind=%d ESR=0x%lx (EC=0x%x) FAR=0x%lx ELR=0x%lx\n",
          kind, esr, ec, far, f->elr);
  if (!from_user) arch_kernel_fault_shell ();       // as on the x86: the kernel shell, no way back to user mode
  for (;;) __asm__ volatile ("wfe");
}

// Every exception is handled with the big kernel lock (see smp.c)
void exc_handler (struct frame *f, int kind) {
  bkl_lock ();
  exc_handler_locked (f, kind);
  if ((f->spsr & 0xf) == 0 && book_scheduler_on) smp_before_return (f);     // returning to user mode
  bkl_unlock ();
}

// The kernel shell runs inside an interrupt handler and waits for keys there: the UART interrupt must come to
// the CPU that runs it (the others wait for the kernel lock).  Afterwards it goes back to CPU 0.
void irq_uart_to_cpu (int cpu) { gic_route (IRQ_UART, cpu); }

// receive interrupts of the UART (console input), enabled once the book's thread data structures exist
void console_irq_init (void) {
  uart_rx_enable ();
  gic_enable (IRQ_UART);
}

void irq_init (void) {
  __asm__ volatile ("msr vbar_el1, %0; isb" :: "r" (vectors));
  gic_init ();
  timer_init (HZ);
  __asm__ volatile ("msr daifclr, #2");     // unmask IRQs
}
