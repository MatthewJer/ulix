// Ulix ARM64 -- exceptions, GICv2, the ARM generic timer, system calls
// (replace IDT, 8259 PIC, 8253 PIT and the int 0x80 handler of the x86 version)

#include "ulix.h"

extern char vectors[];
extern int  book_scheduler_on;
extern void book_timer_tick (void);
extern void book_syscall (void *frame);
extern void book_user_fault (void *frame);
extern int  book_current_pid (void);
extern void arch_signal_check (void *frame);
extern void uart_rx_irq (void);
extern void uart_rx_enable (void);

// ---- GICv2 on qemu "virt" ------------------------------------------------
#define GICD_BASE   (KERNEL_VA + 0x08000000UL)
#define GICC_BASE   (KERNEL_VA + 0x08010000UL)
#define GICD_CTLR       (*(volatile u32 *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n) (*(volatile u32 *)(GICD_BASE + 0x100 + 4*(n)))
#define GICD_IPRIORITYR(n)(*(volatile u32 *)(GICD_BASE + 0x400 + 4*(n)))
#define GICC_CTLR       (*(volatile u32 *)(GICC_BASE + 0x000))
#define GICC_PMR        (*(volatile u32 *)(GICC_BASE + 0x004))
#define GICC_IAR        (*(volatile u32 *)(GICC_BASE + 0x00c))
#define GICC_EOIR       (*(volatile u32 *)(GICC_BASE + 0x010))

#define IRQ_TIMER   30      // EL1 physical timer = PPI 14 = INTID 30
#define IRQ_UART    33      // PL011 UART0 = SPI 1 = INTID 33

static void gic_enable (int irq) {
  GICD_IPRIORITYR (irq / 4) &= ~(0xffU << ((irq % 4) * 8));   // highest priority
  GICD_ISENABLER (irq / 32) = 1U << (irq % 32);
}

static void gic_init (void) {
  GICD_CTLR = 1;
  GICC_PMR  = 0xff;         // accept all priorities
  GICC_CTLR = 1;
}

// ---- generic timer -------------------------------------------------------
#define HZ 100
static u64 timer_ticks_per_irq;
volatile u64 ticks = 0;

static void timer_reload (void) {
  __asm__ volatile ("msr cntp_tval_el0, %0" :: "r" (timer_ticks_per_irq));
}

static void timer_init (int hz) {
  u64 freq;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  timer_ticks_per_irq = freq / hz;
  printf ("Timer: %lu Hz counter, %d interrupts/s\n", freq, hz);
  timer_reload ();
  __asm__ volatile ("msr cntp_ctl_el0, %0" :: "r" (1UL));    // enable, unmasked
}

// ---- exception entry -----------------------------------------------------
volatile int expect_fault = 0, faults = 0, procs_killed = 0;
volatile u64 fault_esr, fault_far;

void exc_handler (struct frame *f, int kind) {
  int from_user = (f->spsr & 0xf) == 0;     // M[3:0] == EL0t

  if (kind == 1) {                          // IRQ
    u32 iar = GICC_IAR;
    u32 irq = iar & 0x3ff;
    if (irq < 1020) GICC_EOIR = iar;        // acknowledge first: nested interrupts must be possible
                                            // (the kernel shell waits for keys inside the UART handler)
    if (irq == IRQ_TIMER) { ticks++; timer_reload (); }
    else if (irq == IRQ_UART) { if (book_scheduler_on) uart_rx_irq (); }
    else if (irq < 1020) printf ("spurious IRQ %u\n", irq);
    if (irq == IRQ_TIMER && book_scheduler_on) book_timer_tick ();   // time slice over (10 ms)
    if (from_user && book_scheduler_on) arch_signal_check (f);       // deliver pending signals
    return;
  }

  u64 esr, far;
  __asm__ volatile ("mrs %0, esr_el1" : "=r" (esr));
  __asm__ volatile ("mrs %0, far_el1" : "=r" (far));
  u32 ec = esr >> 26;

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
  for (;;) __asm__ volatile ("wfe");
}

// receive interrupts of the UART (console input), enabled once the book's thread data structures exist
void console_irq_init (void) {
  uart_rx_enable ();
  gic_enable (IRQ_UART);
}

void irq_init (void) {
  __asm__ volatile ("msr vbar_el1, %0; isb" :: "r" (vectors));
  gic_init ();
  gic_enable (IRQ_TIMER);
  timer_init (HZ);
  __asm__ volatile ("msr daifclr, #2");     // unmask IRQs
}
