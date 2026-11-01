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
  struct file files[MAX_FDS];
};

extern void cpu_switch (struct ctx *old, struct ctx *new);
extern void exit_exception (void);

static struct task tasks[MAX_TASKS];
static struct task *current = &tasks[0];    // slot 0 is the idle task = boot thread
static int next_pid = 1;
static int exit_codes[128];
int context_switches = 0, tasks_reaped = 0, preemptions = 0;
static int from_timer = 0;

void sched_init (void) {
  tasks[0].state = T_RUNNABLE;
  tasks[0].pid   = 0;
  tasks[0].l0    = empty_l0_pa;
}

int task_pid (void) { return current->pid; }

static struct task *slot_alloc (void) {
  for (int i = 1; i < MAX_TASKS; i++) if (tasks[i].state == T_FREE) return &tasks[i];
  return 0;
}

// Give the task a kernel stack with a prepared frame: the first switch "returns" through
// exit_exception into user mode (eret to EL0t, interrupts enabled). x0/x1 are the first
// two arguments of the user entry point (argc/argv for ELF programs).
static int task_finish (struct task *t, u64 entry, u64 user_sp, u64 arg0, u64 arg1) {
  t->kstack_pa = alloc_frames_contig (KSTACK_PAGES);
  u64 ktop = (u64)KVA (t->kstack_pa) + KSTACK_PAGES * PAGE_SIZE;
  struct frame *f = (struct frame *)(ktop - sizeof (struct frame));
  f->x[0] = arg0;
  f->x[1] = arg1;
  f->elr  = entry;
  f->spsr = 0;
  for (int i = 0; i < 10; i++) t->ctx.x19_x28[i] = 0;
  t->ctx.x29 = 0;
  t->ctx.x30 = (u64)exit_exception;
  t->ctx.sp  = (u64)f;
  t->ctx.sp_el0 = user_sp;
  for (int i = 0; i < MAX_FDS; i++) t->files[i].ino = 0;
  t->pid = next_pid++;
  exit_codes[t->pid & 127] = 0;
  t->state = T_RUNNABLE;
  return t->pid;
}

// built-in program (a copy of a code blob from the kernel image)
int task_create_user (const void *code_start, const void *code_end, u64 arg) {
  struct task *t = slot_alloc ();
  if (!t) return -1;
  t->l0 = new_user_l0 ();
  u64 code_pa  = alloc_frame ();
  u64 stack_pa = alloc_frame ();
  u8 *src = (u8 *)code_start, *dst = KVA (code_pa);
  for (u64 i = 0; i < (u64)((u8 *)code_end - src); i++) dst[i] = src[i];
  sync_icache (dst, PAGE_SIZE);
  map_page_in (t->l0, USER_CODE, code_pa, PAGE_URX);
  map_page_in (t->l0, USER_STACK_TOP - PAGE_SIZE, stack_pa, PAGE_URW);
  return task_finish (t, USER_CODE, USER_STACK_TOP, arg, 0);
}

#define USER_STACK_PAGES 4

// start an AArch64 ELF program from the file system; argv is copied to the new stack
int task_create_elf (const char *path, int argc, const char *const *argv) {
  struct task *t = slot_alloc ();
  if (!t) return -1;
  u32 ino = minix_lookup (path);
  if (!ino || !minix_is_file (ino)) { printf ("exec: %s: no such file\n", path); return -1; }

  u64 l0 = new_user_l0 (), entry;
  if (elf_load (l0, ino, &entry) != 0) { free_user_space (l0); return -1; }

  u64 top_pa = 0;
  for (int i = 1; i <= USER_STACK_PAGES; i++) {
    u64 pa = alloc_frame ();
    if (i == 1) top_pa = pa;                         // the page just below USER_STACK_TOP
    map_page_in (l0, USER_STACK_TOP - i * PAGE_SIZE, pa, PAGE_URW);
  }

  // build argc/argv at the top of the stack: strings first, then the pointer array
  u8 *page = KVA (top_pa);
  u64 base = USER_STACK_TOP - PAGE_SIZE, sp = USER_STACK_TOP;
  u64 ptrs[16];
  if (argc > 15) argc = 15;
  for (int i = argc - 1; i >= 0; i--) {
    int len = 0; while (argv[i][len]) len++;
    sp -= len + 1;
    if (sp < base + 512) { free_user_space (l0); printf ("exec: arguments too long\n"); return -1; }
    for (int k = 0; k <= len; k++) page[sp - base + k] = argv[i][k];
    ptrs[i] = sp;
  }
  sp &= ~15UL;
  sp -= ((argc + 1) * 8 + 15) & ~15UL;
  u64 *av = (u64 *)(page + (sp - base));
  for (int i = 0; i < argc; i++) av[i] = ptrs[i];
  av[argc] = 0;

  t->l0 = l0;
  return task_finish (t, entry, sp, argc, sp);
}

// ---- per-task file descriptors ----
int task_fd_open (u32 ino) {
  for (int i = 0; i < MAX_FDS; i++)
    if (!current->files[i].ino) { current->files[i].ino = ino; current->files[i].off = 0; return i; }
  return -1;
}

struct file *task_fd_get (int fd) {
  if (fd < 0 || fd >= MAX_FDS || !current->files[fd].ino) return 0;
  return &current->files[fd];
}

int task_fd_close (int fd) {
  if (!task_fd_get (fd)) return -1;
  current->files[fd].ino = 0;
  return 0;
}

int task_exit_code (int pid) { return exit_codes[pid & 127]; }

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

void task_exit (int code) {
  exit_codes[current->pid & 127] = code;
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
