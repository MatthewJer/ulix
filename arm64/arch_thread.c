// Ulix ARM64 -- platform side of the book's thread code (ulix-book.nw: scheduler, queues, mutexes, ...)
//
// The book's scheduler() picks the next thread (round robin over the ready queue, zombie cleanup)
// and then calls arch_context_switch(); the x86 version of that step swaps ESP/EBP, CR3 and the
// TSS inside the interrupt handler.  Here every thread has its own kernel stack, and a switch
// saves the callee-saved registers, SP and SP_EL0 of the old thread and loads those of the new
// one (cpu_switch in vectors.S).  Like on the x86, kernel code runs with interrupts masked: the
// timer interrupt only switches threads when it arrives while the idle thread waits in 'wfi'
// (or in user mode, later), so the queues of the book need no further locking.

#include "ulix-api.h"

typedef unsigned long u64;
#define KVA(pa)  ((void *)(0xFFFF000000000000UL + (u64)(pa)))
#define PA(va)   ((u64)(va) - 0xFFFF000000000000UL)

struct kctx { u64 x19_x28[10]; u64 x29, x30, sp, sp_el0; };      // layout known to cpu_switch

extern void cpu_switch (struct kctx *old, struct kctx *new);
extern u64  alloc_frames_contig (int n);
extern void free_frame (u64 pa);

#define KSTACK_PAGES_ARCH 4
#define PAGE 4096UL

static struct kctx kctx[MAX_THREADS];
static u64 kstack_pa[MAX_THREADS];             // physical address of the thread's kernel stack, 0 = none
int book_scheduler_on = 0;                     // irq.c asks this before calling the book's scheduler

// ---- hooks called from the book ----

int arch_switch_count = 0;

void arch_context_switch (TCB *t_old, TCB *t_new, context_t *r) {
  arch_switch_count++;
  current_task = t_new->tid;
  cpu_switch (&kctx[t_old->tid], &kctx[t_new->tid]);
}

static inline u64 irq_save (void) {
  u64 daif;
  __asm__ volatile ("mrs %0, daif; msr daifset, #2" : "=r" (daif) :: "memory");
  return daif;
}

static inline void irq_restore (u64 daif) {
  __asm__ volatile ("msr daif, %0" :: "r" (daif) : "memory");
}

// kernel code asks for a reschedule (the x86 version does int 0x80 / syscall_resign here)
void arch_resign (void) {
  u64 flags = irq_save ();
  scheduler (0, SCHED_SRC_RESIGN);
  irq_restore (flags);
}

void book_timer_tick (void) {                  // called by the timer interrupt, interrupts are masked
  scheduler (0, SCHED_SRC_TIMER);
}

// ---- threads ----

void thread_trampoline (void);                 // asm below: x19 = entry, x20 = argument
__asm__ (
  ".text\n"
  ".global thread_trampoline\n"
  "thread_trampoline:\n"
  "  mov x0, x20\n"
  "  blr x19\n"
  "  bl  arch_thread_exit\n"
  "1: b 1b\n"
);

void arch_thread_exit (void) {
  thread_id tid = current_task;
  remove_from_ready_queue (tid);
  thread_table[tid].state = TSTATE_ZOMBIE;     // ppid == 1: the scheduler removes the zombie
  scheduler (0, SCHED_SRC_RESIGN);
  for (;;) __asm__ volatile ("wfe");
}

// the boot thread becomes thread 1, the idle thread of the book
void arch_threads_init (void) {
  for (int i = 0; i < MAX_THREADS; i++) { thread_table[i].used = false; }
  thread_table[0].prev = 0;
  thread_table[0].next = 0;
  address_spaces[1].status = AS_USED;          // address space 1 = the kernel (no user pages)
  TCB *idle = &thread_table[1];
  idle->used = true;
  idle->tid = idle->pid = 1;
  idle->ppid = 0;
  idle->addr_space = 1;
  idle->new = false;
  memcpy (idle->cmdline, "idle", 5);
  add_to_ready_queue (1);
  current_task = 1;
  scheduler_is_active = true;
  book_scheduler_on = 1;
}

// start a kernel thread running entry (arg); returns the thread ID or -1
int arch_thread_create (void (*entry)(void *), void *arg, const char *name) {
  thread_id tid;
  for (tid = 2; tid < MAX_THREADS; tid++) if (!thread_table[tid].used) break;
  if (tid == MAX_THREADS) return -1;

  if (kstack_pa[tid]) {                        // stack of a finished thread that used this ID before
    for (int k = 0; k < KSTACK_PAGES_ARCH; k++) free_frame (kstack_pa[tid] + k * PAGE);
  }
  kstack_pa[tid] = alloc_frames_contig (KSTACK_PAGES_ARCH);
  u64 top = (u64)KVA (kstack_pa[tid]) + KSTACK_PAGES_ARCH * PAGE;
  struct kctx *c = &kctx[tid];
  for (int i = 0; i < 10; i++) c->x19_x28[i] = 0;
  c->x19_x28[0] = (u64)entry;                  // x19
  c->x19_x28[1] = (u64)arg;                    // x20
  c->x29 = 0;
  c->x30 = (u64)thread_trampoline;
  c->sp  = top;
  c->sp_el0 = 0;

  TCB *t = &thread_table[tid];
  memset (t, 0, sizeof (TCB));
  t->used = true;
  t->tid = t->pid = tid;
  t->ppid = 1;                                 // children of the idle thread are not waited for
  t->addr_space = 1;
  t->new = false;
  for (int i = 0; name[i] && i < 10; i++) t->cmdline[i] = name[i];
  u64 flags = irq_save ();
  add_to_ready_queue (tid);
  irq_restore (flags);
  return tid;
}

// free the kernel stacks of finished threads (the thread cannot do that itself)
int arch_threads_reap (void) {
  int n = 0;
  u64 flags = irq_save ();
  for (thread_id tid = 2; tid < MAX_THREADS; tid++) {
    if (kstack_pa[tid] && !thread_table[tid].used) {
      for (int k = 0; k < KSTACK_PAGES_ARCH; k++) free_frame (kstack_pa[tid] + k * PAGE);
      kstack_pa[tid] = 0;
      n++;
    }
  }
  irq_restore (flags);
  return n;
}
