// Ulix ARM64 -- tasks, round-robin scheduler, kernel context switch
// (replaces the x86 TSS-based task switching; the user state lives in the
// register frame on the task's kernel stack, the kernel state in 'struct ctx')

#include "ulix.h"

#define MAX_TASKS     8
#define KSTACK_PAGES  4

enum { T_FREE = 0, T_RUNNABLE, T_ZOMBIE };

struct ctx {                                // layout is known to cpu_switch (vectors.S)
  u64 x19_x28[10];
  u64 x29, x30, sp, sp_el0;
};

struct task {
  struct ctx ctx;
  int  state;
  int  pid;
  u64  l0;                                  // physical address of the TTBR0 root table
  u64  kstack_pa;
};

extern void cpu_switch (struct ctx *old, struct ctx *new);
extern void exit_exception (void);

static struct task tasks[MAX_TASKS];
static struct task *current = &tasks[0];    // slot 0 is the idle task = boot thread
static int next_pid = 1;
int context_switches = 0, tasks_reaped = 0, preemptions = 0;
static int from_timer = 0;

void sched_init (void) {
  tasks[0].state = T_RUNNABLE;
  tasks[0].pid   = 0;
  tasks[0].l0    = empty_l0_pa;
}

int task_pid (void) { return current->pid; }

int task_create_user (const void *code_start, const void *code_end, u64 arg) {
  int slot = 1;
  while (slot < MAX_TASKS && tasks[slot].state != T_FREE) slot++;
  if (slot == MAX_TASKS) return -1;
  struct task *t = &tasks[slot];

  // address space: one code page (private copy of the program) and one stack page
  t->l0 = new_user_l0 ();
  u64 code_pa  = alloc_frame ();
  u64 stack_pa = alloc_frame ();
  u8 *src = (u8 *)code_start, *dst = KVA (code_pa);
  for (u64 i = 0; i < (u64)((u8 *)code_end - src); i++) dst[i] = src[i];
  sync_icache (dst, PAGE_SIZE);
  map_page_in (t->l0, USER_CODE, code_pa, PAGE_URX);
  map_page_in (t->l0, USER_STACK_TOP - PAGE_SIZE, stack_pa, PAGE_URW);

  // kernel stack with a prepared frame: the first switch "returns" through exit_exception
  // into user mode (eret to EL0t, interrupts enabled)
  t->kstack_pa = alloc_frames_contig (KSTACK_PAGES);
  u64 ktop = (u64)KVA (t->kstack_pa) + KSTACK_PAGES * PAGE_SIZE;
  struct frame *f = (struct frame *)(ktop - sizeof (struct frame));
  f->x[0] = arg;
  f->elr  = USER_CODE;
  f->spsr = 0;
  for (int i = 0; i < 10; i++) t->ctx.x19_x28[i] = 0;
  t->ctx.x29 = 0;
  t->ctx.x30 = (u64)exit_exception;
  t->ctx.sp  = (u64)f;
  t->ctx.sp_el0 = USER_STACK_TOP;

  t->pid = next_pid++;
  t->state = T_RUNNABLE;
  return t->pid;
}

void schedule (void) {
  struct task *prev = current, *next = 0;
  int cur = prev - tasks;
  for (int i = 1; i <= MAX_TASKS && !next; i++) {
    int j = (cur + i) % MAX_TASKS;
    if (j != 0 && tasks[j].state == T_RUNNABLE) next = &tasks[j];
  }
  if (!next) next = &tasks[0];              // nothing to run: idle
  int preempt = from_timer && prev->state == T_RUNNABLE && prev != &tasks[0];
  from_timer = 0;
  if (next == prev) return;
  current = next;
  context_switches++;
  if (preempt) preemptions++;             // a running user task lost the CPU to the timer
  u64 ttbr = next->l0 | ((u64)next->pid << 48);     // ASID = pid: no TLB flush needed
  __asm__ volatile ("msr ttbr0_el1, %0; isb" :: "r" (ttbr));
  cpu_switch (&prev->ctx, &next->ctx);
}

// called by the timer interrupt: the time slice of the running task is over
void schedule_preempt (void) {
  from_timer = 1;
  schedule ();
}

void task_exit (void) {
  current->state = T_ZOMBIE;
  schedule ();
  for (;;) __asm__ volatile ("wfe");
}

int tasks_live (void) {
  int n = 0;
  for (int i = 1; i < MAX_TASKS; i++) if (tasks[i].state != T_FREE) n++;
  return n;
}

// free what a dead task cannot free itself (it is still running on its kernel stack
// while exiting); called from the idle loop
void tasks_reap (void) {
  for (int i = 1; i < MAX_TASKS; i++) {
    struct task *t = &tasks[i];
    if (t->state != T_ZOMBIE) continue;
    free_user_space (t->l0);
    __asm__ volatile ("dsb ishst; tlbi aside1is, %0; dsb ish; isb" :: "r" ((u64)t->pid << 48));
    for (int k = 0; k < KSTACK_PAGES; k++) free_frame (t->kstack_pa + k * PAGE_SIZE);
    t->state = T_FREE;
    tasks_reaped++;
  }
}
