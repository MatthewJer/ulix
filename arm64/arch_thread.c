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
#include "arch_pte.h"

#define KVA(pa)  ((void *)(0xFFFF000000000000UL + (u64)(pa)))
#define PA(va)   ((u64)(va) - 0xFFFF000000000000UL)

struct kctx { u64 x19_x28[10]; u64 x29, x30, sp, sp_el0; };      // layout known to cpu_switch

extern void cpu_switch (struct kctx *old, struct kctx *new);
extern u64  alloc_frames_contig (int n);
extern void free_frame (u64 pa);
extern void get_frame (u64 pa);
extern int  cow_fault (u64 l0_pa, u64 va);
extern u64  alloc_frame (void);
extern u64  new_user_l0 (void);
extern void free_user_space (u64 l0_pa);
extern int  map_page_in (u64 l0_pa, u64 va, u64 pa, u64 flags);
extern void sync_icache (void *p, u64 len);
extern int  elf_load (u64 l0, unsigned ino, u64 *entry, u64 *image_end);
extern unsigned minix_lookup (const char *path);
extern int  minix_is_file (unsigned ino);
extern u64  empty_l0_pa;
extern void exit_exception (void);

#define KSTACK_PAGES_ARCH 4
#define PAGE 4096UL

static struct kctx kctx[MAX_THREADS];
static void arch_syscalls_init (void);
void arch_resign (void);
int arch_threads_reap (void);
void syscall_sigreturn (context_t *r);
void arch_clock_init (void);
void arch_syscall_clrscr (context_t *r);
void arch_syscall_idle (context_t *r);
void arch_syscall_ignore (context_t *r);
void arch_syscall_free_frames (context_t *r);
extern long frames_in_use;
void syscall_readchar (context_t *r);
void syscall_setterm (context_t *r);
void syscall_handler (context_t *r);          // defined in the book without a prototype
static u64 kstack_pa[MAX_THREADS];
static addr_space_id kstack_as[MAX_THREADS];   // address space of the process the stack belongs to             // physical address of the thread's kernel stack, 0 = none
int book_scheduler_on = 0;                     // irq.c asks this before calling the book's scheduler

// ---- hooks called from the book ----

int arch_switch_count = 0;

static u64 as_l0[MAX_ADDR_SPACES];             // physical address of the TTBR0 root table of each address space

// Make an address space the active one.  Every address space uses its own number as ASID and its user
// pages are non-global, so the TLB is not flushed on a switch.  A CPU without 16-bit ASIDs falls back to
// ASID 0 and a flush on every switch.
extern int asid_16bit;

static void asid_flush (addr_space_id id) {             // forget all TLB entries of one address space
  if (asid_16bit) __asm__ volatile ("dsb ishst; tlbi aside1is, %0; dsb ish; isb" :: "r" ((u64)id << 48) : "memory");
  else            __asm__ volatile ("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

static void arch_activate_as (addr_space_id id) {
  current_as = id;
  if (asid_16bit)
    __asm__ volatile ("msr ttbr0_el1, %0; isb" :: "r" (as_l0[id] | ((u64)id << 48)) : "memory");
  else
    __asm__ volatile ("msr ttbr0_el1, %0; isb; tlbi vmalle1; dsb nsh; isb" :: "r" (as_l0[id]) : "memory");
}

void arch_context_switch (TCB *t_old, TCB *t_new, context_t *r) {
  arch_switch_count++;
  current_task = t_new->tid;
  if (t_old->addr_space != t_new->addr_space || current_as != t_new->addr_space)
    arch_activate_as (t_new->addr_space);
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
  as_l0[1] = empty_l0_pa;
  current_as = 1;
  arch_syscalls_init ();
  TCB *idle = &thread_table[1];
  idle->used = true;
  idle->tid = idle->pid = 1;
  idle->ppid = 0;
  idle->addr_space = 1;
  idle->new = false;
  memcpy (idle->cmdline, "idle", 5);
  add_to_ready_queue (1);
  current_task = 1;
  next_pid = 3;                                // pids 1 (idle) and 2 (init) are protected by the book's kill()
  arch_clock_init ();                          // system_start_time from the real-time clock
  current_mounts = 1;                          // the book mounts three disks; there is one: / (its /tmp and /mnt are plain directories)
  buffer_lock = get_new_lock ("buffer");       // the book creates it when it initializes the buffer cache
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
      // a process killed by another one (u_kill) never ran syscall_exit: free its address space here
      addr_space_id as = kstack_as[tid];
      if (as >= 2 && address_spaces[as].status == AS_USED && address_spaces[as].pid == (int)tid)
        destroy_address_space (as);
      kstack_as[tid] = 0;
      for (int k = 0; k < KSTACK_PAGES_ARCH; k++) free_frame (kstack_pa[tid] + k * PAGE);
      kstack_pa[tid] = 0;
      n++;
    }
  }
  irq_restore (flags);
  return n;
}


// =====================================================================================
// Processes: address spaces, fork, exec, first process, system calls
// (the x86 versions of these functions live in the book; here they are written for the
// 4-level AArch64 page tables, but keep the same names and data structures)
// =====================================================================================

#define USER_STACK_TOP    0x80000000UL
#define USER_STACK_PAGES  4
typedef char assert_context_size[(sizeof (context_t) == 288) ? 1 : -1];   // must match vectors.S

static int as_alloc (void) {
  for (int id = 2; id < MAX_ADDR_SPACES; id++) {
    if (address_spaces[id].status == AS_FREE) {
      memset (&address_spaces[id], 0, sizeof (address_space));
      address_spaces[id].status    = AS_USED;
      address_spaces[id].pid       = -1;
      address_spaces[id].refcount  = 1;
      address_spaces[id].stacksize = USER_STACK_PAGES * PAGE;
      as_l0[id] = 0;
      return id;
    }
  }
  return -1;
}

// the book calls this from syscall_exit(); we may be running in the address space that is destroyed
void destroy_address_space (addr_space_id id) {
  if (id < 2 || address_spaces[id].status == AS_FREE) return;
  if (--address_spaces[id].refcount > 0) {           // other threads still use it (as in the book)
    address_spaces[id].pid = -1;                     // the owner is gone: arch_threads_reap must not destroy it
    return;
  }
  if (current_as == id) arch_activate_as (1);        // leave it first: TTBR0 = empty table
  if (as_l0[id]) { asid_flush (id); free_user_space (as_l0[id]); }
  as_l0[id] = 0;
  address_spaces[id].status = AS_FREE;
}

// Duplicate a user address space for fork(): the child gets the same frames. Read-only pages (the code) are
// shared for good; writable pages become copy-on-write in parent AND child (both entries read-only, marked
// PTE_COW, the frame has one more reference), the copy is made by cow_fault() at the first write.
static int clone_level (u64 old_pa, u64 new_l0, int lvl, u64 va_prefix) {
  u64 *t = KVA (old_pa);
  for (int i = 0; i < 512; i++) {
    u64 e = t[i];
    if (!(e & PTE_VALID)) continue;
    u64 va = va_prefix | ((u64)i << (39 - 9 * lvl));
    if (lvl < 3) {
      if (clone_level (e & PTE_ADDR, new_l0, lvl + 1, va) < 0) return -1;
    } else {
      u64 pa = e & PTE_ADDR;
      if (!(e & PTE_RO)) {                                  // writable: copy-on-write from now on
        e |= PTE_RO | PTE_COW;
        t[i] = e;                                           // the parent's entry, too
      }
      get_frame (pa);
      if (map_page_in (new_l0, va, pa, e & ~PTE_ADDR) != 0) { free_frame (pa); return -1; }
    }
  }
  return 0;
}

// fault handler for write accesses to shared pages (called from the exception handler)
int arch_cow_fault (u64 va) {
  return cow_fault (as_l0[current_as], va);
}

// Map the user stack and build  [argc][argv]  at its top, with the argument strings and the
// argv array above it.  The start code (crt0) loads argc and argv from there.
// Returns the initial user stack pointer, 0 on error.
#define SIGPAGE_VA   0x7FFF0000UL            // the "signal trampoline" page, mapped in every process
#define ARCH_NR_SIGRETURN 600                // AArch64 only: return from a signal handler

static int map_sigpage (u64 l0) {
  u64 pa = alloc_frame ();
  unsigned int *code = KVA (pa);
  code[0] = 0xD2800000 | ((ARCH_NR_SIGRETURN & 0xffff) << 5);   // movz x0, #600
  code[1] = 0xD4000001;                      // svc #0
  code[2] = 0xD503201F;                      // nop (never reached: sigreturn does not come back)
  sync_icache (code, PAGE);
  return map_page_in (l0, SIGPAGE_VA, pa, PAGE_URX);
}

static u64 build_user_stack (u64 l0, int argc, char **argv) {
  u64 top_pa = 0;
  if (map_sigpage (l0) != 0) return 0;
  for (int i = 1; i <= USER_STACK_PAGES; i++) {
    u64 pa = alloc_frame ();
    if (i == 1) top_pa = pa;
    if (map_page_in (l0, USER_STACK_TOP - i * PAGE, pa, PAGE_URW) != 0) return 0;
  }
  byte *page = KVA (top_pa);
  // Leave some room above the strings: programs like ls append characters to their argv strings
  // (the x86 kernel puts the arguments 512 bytes below the top of the stack, too).
  u64 base = USER_STACK_TOP - PAGE, sp = USER_STACK_TOP - 512, ptrs[16];
  if (argc > 15) argc = 15;
  for (int i = argc - 1; i >= 0; i--) {
    int len = strlen (argv[i]) + 1;
    sp -= len;
    if (sp < base + 512) return 0;                       // arguments too long
    memcpy (page + (sp - base), argv[i], len);
    ptrs[i] = sp;
  }
  sp &= ~15UL;
  sp -= ((argc + 1) * 8 + 15) & ~15UL;
  u64 *av = (u64 *)(page + (sp - base));
  for (int i = 0; i < argc; i++) av[i] = ptrs[i];
  av[argc] = 0;
  u64 argv_va = sp;
  sp -= 16;
  u64 *top = (u64 *)(page + (sp - base));
  top[0] = argc;
  top[1] = argv_va;
  return sp;
}

// The first switch to a new process does not come from an exception, so the check for pending
// signals at the end of the exception handler is not run.  A signal that was sent before the
// process ever ran (typically to a freshly forked child) would stay pending, so the first
// "return" goes through this stub: sp points to the prepared exception frame.
void process_first_return (void);
void arch_signal_check (context_t *f);
__asm__ (
  ".text\n"
  ".global process_first_return\n"
  "process_first_return:\n"
  "  mov x0, sp\n"
  "  bl  arch_signal_check\n"
  "  b   exit_exception\n"
);

// kernel stack for a user process with a prepared exception frame: the first switch to the
// thread "returns" through exit_exception (vectors.S) into user mode
static void kstack_for_process (thread_id tid, const context_t *frame, int owns_as);
static void kstack_for_process (thread_id tid, const context_t *frame, int owns_as) {
  kstack_as[tid] = owns_as ? thread_table[tid].addr_space : 0;   // 0: an extra thread, the address space belongs to its process
  if (owns_as) address_spaces[kstack_as[tid]].pid = tid;        // marks who owns the address space (see arch_threads_reap)
  if (kstack_pa[tid]) {
    for (int k = 0; k < KSTACK_PAGES_ARCH; k++) free_frame (kstack_pa[tid] + k * PAGE);
  }
  kstack_pa[tid] = alloc_frames_contig (KSTACK_PAGES_ARCH);
  u64 top = (u64)KVA (kstack_pa[tid]) + KSTACK_PAGES_ARCH * PAGE;
  context_t *f = (context_t *)(top - sizeof (context_t));
  *f = *frame;
  struct kctx *c = &kctx[tid];
  for (int i = 0; i < 10; i++) c->x19_x28[i] = 0;
  c->x29 = 0;
  c->x30 = (u64)process_first_return;
  c->sp  = (u64)f;
  c->sp_el0 = f->sp_el0;
}

static void tcb_init_process (TCB *t, thread_id tid, thread_id ppid, const char *name) {
  addr_space_id as = t->addr_space;
  memset (t, 0, sizeof (TCB));
  t->used = true;
  t->addr_space = as;
  t->tid = t->pid = tid;
  t->ppid = ppid;
  t->terminal = 0;
  memcpy (t->cwd, "/", 2);
  for (int i = 0; name[i] && i < 20; i++) t->cmdline[i] = name[i];
  t->files[0] = DEV_STDIN;  t->files[1] = DEV_STDOUT;  t->files[2] = DEV_STDERR;
  for (int i = 3; i < MAX_PFD; i++) t->files[i] = -1;
}

// Translate a virtual address of an address space to a physical one (the book's mmu() for the x86
// page tables); 0 if the address is not mapped.  Used by the kernel shell command 'listas'.
memaddress mmu (addr_space_id as, memaddress vaddr) {
  if (vaddr >= 0xFFFF000000000000UL) return vaddr - 0xFFFF000000000000UL;    // linear map of the kernel
  if (as < 1 || !as_l0[as]) return 0;
  u64 *t = KVA (as_l0[as]);
  for (int lvl = 0; lvl < 4; lvl++) {
    u64 e = t[(vaddr >> (39 - 9 * lvl)) & 511];
    if (!(e & PTE_VALID)) return 0;
    if (lvl == 3) return (e & PTE_ADDR) | (vaddr & 0xfff);
    t = KVA (e & PTE_ADDR);
  }
  return 0;
}

// Kernel shell command 'mem': list the mapped ranges of an address space, merged where the pages are
// contiguous in virtual address and have the same flags.
static void dump_level (u64 table_pa, int lvl, u64 prefix, u64 *run_start, u64 *run_end, u64 *run_flags, int *pages) {
  u64 *t = KVA (table_pa);
  for (int i = 0; i < 512; i++) {
    u64 e = t[i];
    if (!(e & PTE_VALID)) continue;
    u64 va = prefix | ((u64)i << (39 - 9 * lvl));
    if (lvl < 3) { dump_level (e & PTE_ADDR, lvl + 1, va, run_start, run_end, run_flags, pages); continue; }
    u64 fl = e & (PTE_RO | PTE_UXN | PTE_PXN | PTE_COW | PTE_AP_USER);
    if (*run_end == va && *run_flags == fl && *run_end != 0) { *run_end = va + PAGE; (*pages)++; continue; }
    if (*run_end) printf ("  %08lx-%08lx  %s%s%s%s  %d page(s)\n", *run_start, *run_end,
                          (*run_flags & PTE_AP_USER) ? "u" : "-", "r", (*run_flags & PTE_RO) ? "-" : "w",
                          (*run_flags & PTE_UXN) ? "-" : "x", *pages);
    *run_start = va; *run_end = va + PAGE; *run_flags = fl; *pages = 1;
  }
}

void arch_dump_as (addr_space_id as) {
  if (as < 1 || !as_l0[as]) { printf ("address space %d has no page table\n", (int)as); return; }
  u64 s = 0, e = 0, f = 0; int pages = 0;
  printf ("address space %d (ASID %d), root table at %lx\n", (int)as, asid_16bit ? (int)as : 0, as_l0[as]);
  dump_level (as_l0[as], 0, 0, &s, &e, &f, &pages);
  if (e) printf ("  %08lx-%08lx  %s%s%s%s  %d page(s)\n", s, e, (f & PTE_AP_USER) ? "u" : "-", "r",
                 (f & PTE_RO) ? "-" : "w", (f & PTE_UXN) ? "-" : "x", pages);
  else   printf ("  (no user pages)\n");
}

// ---- fork ----
int arch_threads_reap (void);
int u_fork (context_t *r) {
  arch_threads_reap ();                   // free the kernel stacks of processes that have ended
  thread_id old_tid = current_task;
  addr_space_id old_as = current_as;

  int new_as = as_alloc ();
  if (new_as == -1) return -1;
  u64 nl0 = new_user_l0 ();
  as_l0[new_as] = nl0;
  int cloned = clone_level (as_l0[old_as], nl0, 0, 0);
  __asm__ volatile ("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");   // the parent's pages are read-only now
  if (cloned < 0) { destroy_address_space (new_as); return -1; }
  address_spaces[new_as].memstart = address_spaces[old_as].memstart;
  address_spaces[new_as].memend   = address_spaces[old_as].memend;

  thread_id new_tid = register_new_tcb (new_as);
  if (new_tid == -1) { destroy_address_space (new_as); return -1; }

  TCB *t_old = &thread_table[old_tid];
  TCB *t_new = &thread_table[new_tid];
  *t_new = *t_old;                        // copy the complete TCB, then fix it up (like the book)
  t_new->used       = true;
  t_new->state      = TSTATE_FORK;
  t_new->tid        = new_tid;
  t_new->addr_space = new_as;
  t_new->pid        = new_tid;
  t_new->ppid       = old_tid;
  for (int pfd = 0; pfd < MAX_PFD; pfd++) {
    int gfd = t_old->files[pfd];
    t_new->files[pfd] = (gfd >= 0) ? u_reopen (gfd) : gfd;
  }

  context_t child = *r;                   // the child continues exactly where the parent is ...
  child.eax = 0;                         // ... but fork() returns 0 there
  kstack_for_process (new_tid, &child, 1);
  add_to_ready_queue (new_tid);           // state becomes READY
  return new_tid;                         // in the parent, fork() returns the child's PID
}

// ---- exec: load a new program into a fresh address space and switch to it ----
int u_execv (char *filename, char *const argv[], memaddress *newstack) {
  static char kargs[2048];
  char *kargv[16];
  int argc = 0, pos = 0;
  for (; argv && argv[argc] && argc < 15; argc++) {      // argv lives in the old address space
    int len = strlen (argv[argc]) + 1;
    if (pos + len > (int)sizeof kargs) return -1;
    memcpy (kargs + pos, argv[argc], len);
    kargv[argc] = kargs + pos;
    pos += len;
  }
  unsigned ino = minix_lookup (filename);
  if (!ino || !minix_is_file (ino)) return -1;

  u64 nl0 = new_user_l0 (), entry, image_end;
  if (elf_load (nl0, ino, &entry, &image_end) != 0) { free_user_space (nl0); return -1; }
  u64 sp = build_user_stack (nl0, argc, kargv);
  if (!sp) { free_user_space (nl0); return -1; }

  u64 old = as_l0[current_as];                           // from here on there is no way back
  as_l0[current_as] = nl0;
  address_spaces[current_as].memstart = 0x400000;
  address_spaces[current_as].memend   = image_end;      // the program break starts behind the image
  asid_flush (current_as);                               // entries of the old program must not survive
  arch_activate_as (current_as);
  free_user_space (old);
  *newstack = sp;
  return (int)entry;
}

// ---- start a first process from a file (the kernel plays the role of its parent) ----
int arch_start_user (const char *path, int argc, char **argv) {
  unsigned ino = minix_lookup (path);
  if (!ino || !minix_is_file (ino)) { printf ("exec: %s: no such file\n", path); return -1; }
  int as = as_alloc ();
  if (as == -1) return -1;
  u64 l0 = new_user_l0 (), entry, image_end = 0;
  as_l0[as] = l0;
  u64 sp = 0;
  if (elf_load (l0, ino, &entry, &image_end) == 0) sp = build_user_stack (l0, argc, argv);
  address_spaces[as].memstart = 0x400000;
  address_spaces[as].memend   = image_end;
  if (!sp) { destroy_address_space (as); return -1; }

  thread_id tid = register_new_tcb (as);
  if (tid == -1) { destroy_address_space (as); return -1; }
  tcb_init_process (&thread_table[tid], tid, 1, path + (path[0] == '/'));   // parent: idle, nobody waits
  context_t f;
  memset (&f, 0, sizeof f);
  f.elr = entry;
  f.spsr = 0;                             // EL0t, interrupts enabled
  f.sp_el0 = sp;
  kstack_for_process (tid, &f, 1);
  u64 flags = irq_save ();
  add_to_ready_queue (tid);
  irq_restore (flags);
  return tid;
}


// =====================================================================================
// POSIX threads: several threads in one address space
// A thread is a TCB with its own kernel stack and user stack that shares the address space (same
// TTBR0 table and ASID, address_spaces[].refcount counts the threads) and the file table of the
// process (its 'pid' is the TID of the main thread; for the main thread pid == tid).  The user stack
// is taken from the heap (u_sbrk), like in the book.  Everything else follows the book, too: the address
// space lives until its last thread has gone (exit() of the main thread does not stop the others).
// =====================================================================================
#define THREAD_STACK_PAGES 4

int arch_pthread_create (u64 start) {
  arch_threads_reap ();
  TCB *to = &thread_table[current_task];
  addr_space_id as = current_as;
  void *ustack = u_sbrk (THREAD_STACK_PAGES * PAGE);
  if (ustack == (void *)-1) return -1;
  thread_id nt = register_new_tcb (as);
  if (nt == -1) return -1;
  TCB *tn = &thread_table[nt];
  *tn = *to;                                   // like the book: copy the TCB, then fix it up
  tn->used = true;
  tn->state = TSTATE_FORK;
  tn->tid = nt;
  tn->addr_space = as;
  tn->pid = to->pid;                           // same process
  tn->ppid = to->ppid;
  tn->sig_pending = 0;
  context_t f;
  memset (&f, 0, sizeof f);
  f.elr = start;
  f.spsr = 0;                                  // EL0t
  f.sp_el0 = ((u64)ustack + THREAD_STACK_PAGES * PAGE) & ~15UL;
  kstack_for_process (nt, &f, 0);
  address_spaces[as].refcount++;
  add_to_ready_queue (nt);
  return nt;
}

void arch_syscall_pthread_create (context_t *r) { r->eax = arch_pthread_create (r->ebx); }

// as in the book: the last thread of a process ends it like exit(), any other one just disappears
void arch_syscall_pthread_exit (context_t *r) {
  if (address_spaces[current_as].refcount == 1) {
    r->ebx = 0;
    syscall_exit (r);
    return;
  }
  TCB *t = &thread_table[current_task];
  remove_from_ready_queue (current_task);
  t->state = TSTATE_EXIT;
  address_spaces[current_as].refcount--;
  if (address_spaces[current_as].pid == current_task) address_spaces[current_as].pid = -1;   // see arch_threads_reap
  t->used = false;                             // the kernel stack is freed by arch_threads_reap
  scheduler (r, SCHED_SRC_RESIGN);
}

// ---- pthread mutexes: the user mutex is the index of a kernel lock (the book stores the pointer in an int) ----
static lock mutex_of (pthread_mutex_t *m) {
  int i = *m;
  if (i < 1 || i >= MAX_LOCKS || !kernel_locks[i].used) return NULL;
  return &kernel_locks[i];
}

void arch_syscall_mutex_init (context_t *r) {
  lock l = get_new_lock ("pthread");
  if (!l) { thread_table[current_task].error = EAGAIN; r->eax = (unsigned long)-1; return; }
  *(pthread_mutex_t *)r->ebx = (pthread_mutex_t)(l - kernel_locks);
  r->eax = 0;
}
void arch_syscall_mutex_lock (context_t *r) {
  lock l = mutex_of ((pthread_mutex_t *)r->ebx);
  if (!l) { thread_table[current_task].error = EINVAL; r->eax = (unsigned long)-1; return; }
  mutex_lock (l);
  r->eax = 0;
}
void arch_syscall_mutex_trylock (context_t *r) {
  lock l = mutex_of ((pthread_mutex_t *)r->ebx);
  if (!l) { thread_table[current_task].error = EINVAL; r->eax = (unsigned long)-1; return; }
  if (mutex_try_lock (l)) r->eax = 0;
  else { thread_table[current_task].error = EBUSY; r->eax = (unsigned long)-1; }
}
void arch_syscall_mutex_unlock (context_t *r) {
  lock l = mutex_of ((pthread_mutex_t *)r->ebx);
  if (!l) { thread_table[current_task].error = EINVAL; r->eax = (unsigned long)-1; return; }
  mutex_unlock (l);
  r->eax = 0;
}
void arch_syscall_mutex_destroy (context_t *r) {
  lock l = mutex_of ((pthread_mutex_t *)r->ebx);
  if (!l) { thread_table[current_task].error = EINVAL; r->eax = (unsigned long)-1; return; }
  l->used = false;
  r->eax = 0;
}


// ---- system calls: the book's dispatcher, called from the exception handler ----
void book_syscall (void *frame) {
  syscall_handler ((context_t *)frame);
}

int book_current_pid (void) { return thread_table[current_task].pid; }

// a user process did something illegal: end it like exit(-1) does
void book_user_fault (void *frame) {
  context_t *r = frame;
  r->ebx = (unsigned long)-1;
  syscall_exit (r);
}

static void arch_syscalls_init (void) {
  install_syscall_handler (__NR_exit,      syscall_exit);
  install_syscall_handler (__NR_fork,      syscall_fork);
  install_syscall_handler (__NR_read,      syscall_read);
  install_syscall_handler (__NR_write,     syscall_write);
  install_syscall_handler (__NR_open,      syscall_open);
  install_syscall_handler (__NR_close,     syscall_close);
  install_syscall_handler (__NR_waitpid,   syscall_waitpid);
  install_syscall_handler (__NR_execve,    syscall_execv);
  install_syscall_handler (__NR_unlink,    syscall_unlink);
  install_syscall_handler (__NR_chdir,     syscall_chdir);
  install_syscall_handler (__NR_lseek,     syscall_lseek);
  install_syscall_handler (__NR_getpid,    syscall_getpid);
  install_syscall_handler (__NR_gettid,    syscall_gettid);
  install_syscall_handler (__NR_getppid,   syscall_getppid);
  install_syscall_handler (__NR_resign,    syscall_resign);
  install_syscall_handler (__NR_stat,      syscall_stat);
  install_syscall_handler (__NR_getcwd,    syscall_getcwd);
  install_syscall_handler (__NR_get_errno, syscall_get_errno);
  install_syscall_handler (__NR_signal,    syscall_signal);
  install_syscall_handler (__NR_kill,      syscall_kill);
  install_syscall_handler (__NR_readchar,  syscall_readchar);
  // file system, identities, process information (used by the programs in lib-build/tools)
  install_syscall_handler (__NR_set_errno, syscall_set_errno);
  install_syscall_handler (__NR_getpsinfo, syscall_getpsinfo);
  install_syscall_handler (__NR_setpsname, syscall_setpsname);
  install_syscall_handler (__NR_isatty,    syscall_isatty);
  install_syscall_handler (__NR_mkdir,     syscall_mkdir);
  install_syscall_handler (__NR_rmdir,     syscall_rmdir);
  install_syscall_handler (__NR_readdir,   syscall_getdent);
  install_syscall_handler (__NR_truncate,  syscall_truncate);
  install_syscall_handler (__NR_ftruncate, syscall_ftruncate);
  install_syscall_handler (__NR_link,      syscall_link);
  install_syscall_handler (__NR_symlink,   syscall_symlink);
  install_syscall_handler (__NR_readlink,  syscall_readlink);
  install_syscall_handler (__NR_diskfree,  syscall_diskfree);
  install_syscall_handler (__NR_sync,      syscall_sync);
  install_syscall_handler (__NR_setuid32,  syscall_setuid);
  install_syscall_handler (__NR_setgid32,  syscall_setgid);
  install_syscall_handler (__NR_setreuid32, syscall_seteuid);
  install_syscall_handler (__NR_setregid32, syscall_setegid);
  install_syscall_handler (__NR_login,     syscall_login);
  install_syscall_handler (__NR_query_ids, syscall_query_ids);
  install_syscall_handler (__NR_chown,     syscall_chown);
  install_syscall_handler (__NR_chmod,     syscall_chmod);
  install_syscall_handler (__NR_brk,       syscall_sbrk);
  install_syscall_handler (__NR_pthread_create,        arch_syscall_pthread_create);
  install_syscall_handler (__NR_pthread_exit,          arch_syscall_pthread_exit);
  install_syscall_handler (__NR_pthread_mutex_init,    arch_syscall_mutex_init);
  install_syscall_handler (__NR_pthread_mutex_lock,    arch_syscall_mutex_lock);
  install_syscall_handler (__NR_pthread_mutex_trylock, arch_syscall_mutex_trylock);
  install_syscall_handler (__NR_pthread_mutex_unlock,  arch_syscall_mutex_unlock);
  install_syscall_handler (__NR_pthread_mutex_destroy, arch_syscall_mutex_destroy);
  // there is no VGA screen: these only exist so that programs written for it keep working
  install_syscall_handler (__NR_idle,      arch_syscall_idle);
  install_syscall_handler (__NR_clrscr,    arch_syscall_clrscr);
  install_syscall_handler (__NR_setterm,   syscall_setterm);
  install_syscall_handler (__NR_get_xy,    arch_syscall_ignore);
  install_syscall_handler (__NR_set_xy,    arch_syscall_ignore);
  install_syscall_handler (__NR_read_screen,  arch_syscall_ignore);
  install_syscall_handler (__NR_write_screen, arch_syscall_ignore);
  install_syscall_handler (__NR_get_free_frames, arch_syscall_free_frames);
  install_syscall_handler (ARCH_NR_SIGRETURN, syscall_sigreturn);
}


// ---- signals ----
// The book's scheduler delivers a signal by pushing the signal number and the old EIP on the user
// stack and jumping to the handler; the handler "returns" to the interrupted code and all
// registers are lost.  Here the whole interrupted register frame is saved on the user stack, the
// handler is called like a C function (x0 = signal number) with the link register pointing to a
// small trampoline page that calls sigreturn, which restores the frame.
extern u64 user_to_phys (u64 va, int write);

void arch_signal_check (context_t *f) {
  TCB *t = &thread_table[current_task];
  if (!t->sig_pending || f->elr >= 0x80000000UL) return;            // nothing pending / not in user mode
  for (int signo = 0; signo < 32; signo++) {
    if (!(t->sig_pending & (1UL << signo))) continue;
    t->sig_pending &= ~(1UL << signo);
    sighandler_t h = t->sighandlers[signo];
    if (h == SIG_DFL || h == SIG_IGN) return;                       // cannot happen (u_kill filters them)
    u64 sp = (f->sp_el0 - sizeof (context_t)) & ~15UL;
    if (!user_to_phys (sp, 1) || !user_to_phys (sp + sizeof (context_t) - 1, 1)) {
      printf ("[kernel] no room on the stack of process %d for signal %d\n", t->pid, signo);
      f->ebx = (unsigned long)-1;
      syscall_exit (f);                                             // does not return
    }
    *(context_t *)sp = *f;                                          // save the interrupted state
    f->elr    = (u64)h;
    f->eax    = signo;                                              // x0: argument of the handler
    f->x30    = SIGPAGE_VA;                                         // the handler returns to the trampoline
    f->sp_el0 = sp;
    return;                                                         // one signal at a time
  }
}

// sigreturn: the user stack pointer points to the saved frame
void syscall_sigreturn (context_t *r) {
  u64 sp = r->sp_el0;
  if ((sp & 15) || !user_to_phys (sp, 0) || !user_to_phys (sp + sizeof (context_t) - 1, 0)) {
    r->ebx = (unsigned long)-1;
    syscall_exit (r);
  }
  context_t saved = *(context_t *)sp;
  saved.spsr = r->spsr;                                             // the saved state must stay EL0t
  if (saved.elr >= 0x80000000UL) saved.elr = 0, saved.ebx = (unsigned long)-1;
  *r = saved;
}


// ---- the heap: sbrk() ----
// The book's u_sbrk() works on x86 page tables.  Here the heap starts at the end of the loaded image
// (address_spaces[].memend) and grows upwards, one fresh frame per page; it cannot shrink.
void *u_sbrk (int incr) {
  addr_space_id as = current_as;
  memaddress old = address_spaces[as].memend;
  if (incr == 0) return (void *)old;
  if (incr < 0) return (void *)-1;                       // shrinking is not supported
  memaddress end = old + incr;
  if (end > USER_STACK_TOP - 0x100000UL) return (void *)-1;     // keep 1 MByte below the stack
  for (u64 va = (old + PAGE - 1) & ~(PAGE - 1); va < end; va += PAGE) {
    if (map_page_in (as_l0[as], va, alloc_frame (), PAGE_URW) != 0) return (void *)-1;
  }
  address_spaces[as].memend = end;
  return (void *)old;
}


// ---- system calls that exist for the VGA screen / the x86 frame table ----
void arch_syscall_clrscr (context_t *r)  { printf ("\033[2J\033[H"); }       // ANSI: clear the terminal
void arch_syscall_ignore (context_t *r)  { r->eax = 0; }
void arch_syscall_free_frames (context_t *r) { r->eax = 32768 - frames_in_use; }   // 128 MByte of RAM

// the idle system call: wait for the next interrupt (x86: sti; hlt; cli)
void arch_syscall_idle (context_t *r) {
  __asm__ volatile ("msr daifclr, #2; wfi; msr daifset, #2" ::: "memory");
}
