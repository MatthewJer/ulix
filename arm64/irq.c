// Ulix ARM64 -- exceptions, GICv2 and the ARM generic timer
// (replace IDT, 8259 PIC and 8253 PIT of the x86 version)

typedef unsigned long u64;
typedef unsigned int  u32;

extern void printf (const char *fmt, ...);
extern char vectors[];

// ---- GICv2 on qemu "virt" ------------------------------------------------
#define KERNEL_VA   0xFFFF000000000000UL
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
volatile int expect_fault = 0, faults = 0;
volatile u64 fault_esr, fault_far;

struct frame { u64 x[31]; u64 elr; u64 spsr; };

void exc_handler (struct frame *f, int kind) {
  if (kind == 1) {                       // IRQ
    u32 iar = GICC_IAR;
    u32 irq = iar & 0x3ff;
    if (irq == IRQ_TIMER) {
      ticks++;
      timer_reload ();
    } else if (irq < 1020) {
      printf ("spurious IRQ %u\n", irq);
    }
    if (irq < 1020) GICC_EOIR = iar;
    return;
  }
  u64 esr, far;
  __asm__ volatile ("mrs %0, esr_el1" : "=r" (esr));
  __asm__ volatile ("mrs %0, far_el1" : "=r" (far));
  if (expect_fault && kind == 0 && (esr >> 26) == 0x25) {   // data abort from the MMU test
    fault_esr = esr; fault_far = far; faults++;
    f->elr += 4;                                             // skip the faulting instruction
    return;
  }
  if (kind == 0 && (esr >> 26) == 0x15) {   // SVC from AArch64: syscall entry later
    printf ("SVC #%u at 0x%lx\n", (u32)(esr & 0xffff), f->elr - 4);
    return;
  }
  printf ("\n*** EXCEPTION kind=%d ESR=0x%lx (EC=0x%x) FAR=0x%lx ELR=0x%lx\n",
          kind, esr, (u32)(esr >> 26), far, f->elr);
  for (;;) __asm__ volatile ("wfe");
}

void irq_init (void) {
  __asm__ volatile ("msr vbar_el1, %0; isb" :: "r" (vectors));
  gic_init ();
  gic_enable (IRQ_TIMER);
  timer_init (HZ);
  __asm__ volatile ("msr daifclr, #2");  // unmask IRQs
}
