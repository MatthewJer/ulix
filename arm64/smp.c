// Ulix ARM64 -- more than one CPU
//
// The kernel of the book was written for one CPU: its queues, tables and the scheduler have no locks.  The
// processors share it with a big kernel lock (BKL):
//   - a CPU takes the lock when it enters the kernel (every exception) and gives it back when it returns to
//     user mode or waits for an interrupt; user programs run in parallel on all CPUs;
//   - current_task and current_as of the book are per CPU: they are swapped in when a CPU takes the lock and
//     saved when it gives it back, so the code of the book sees "its" thread, as on one CPU;
//   - the book's scheduler picks the next thread from the ready queue.  Threads that run on another CPU are in
//     that queue, too: while the scheduler chooses, they are marked TSTATE_ONCPU, which the scheduler skips
//     (it only takes threads in state READY).  The book's scheduler () is wrapped, see "scheduler" below;
//   - every CPU has its own idle context (the idle thread 1 of the book is the boot CPU's);
//   - a signal for a thread that runs on another CPU is handed to that CPU (an interrupt makes it enter the
//     kernel), so that a thread is never ended or its address space freed under the feet of another CPU.
// Secondary CPUs are started with PSCI (CPU_ON), as the device tree says.

#define hash smp_hash                      // (the book defines hash () in its header: keep this copy private)
#include "ulix-api.h"
#undef hash
#include "smp.h"
#include "gic.h"

#define TSTATE_ONCPU 20

extern int plat_psci (void), plat_psci_hvc (void), plat_ncpus (void);   // dt.c
extern unsigned plat_psci_cpu_on (void);
extern unsigned long plat_cpu_mpidr (int i);
#define MAX_CPUS    8

struct percpu {
  u64 mpidr;
  volatile int online;
  volatile int in_kernel;                  // between the start of an exception and the return to user mode
  volatile u32 entries;                    // number of kernel entries
  volatile u32 switches;                   // context switches made on this CPU
  thread_id cur_task;  addr_space_id cur_as;
  int depth;                               // how often this CPU took the lock (nested exceptions)
};
static struct percpu pc[MAX_CPUS];
static int ncpus_online = 1;

extern void secondary_entry (void);
extern u64  secondary_tcr;
extern void book_scheduler (context_t *r, int source);
extern int  book_u_kill (int pid, int signo);
extern void arch_sync_ttbr0 (void);
extern void irq_init_cpu (void);

int smp_cpu (void) { u64 v; __asm__ volatile ("mrs %0, tpidr_el1" : "=r" (v)); return (int)v; }
u64 smp_mpidr (int cpu) { return pc[cpu].mpidr; }
int smp_online_cpus (void) { return ncpus_online; }

// ---- the big kernel lock ----
static volatile int bkl_word = 0;
static volatile int bkl_owner = -1;

void bkl_lock (void) {
  int c = smp_cpu ();
  if (bkl_owner == c) { pc[c].depth++; return; }                 // nested exception, or kernel code taking it again
  pc[c].in_kernel = 1;
  pc[c].entries++;
  while (__sync_lock_test_and_set (&bkl_word, 1)) {
    for (u64 spin = 0; bkl_word; spin++) {
      __asm__ volatile ("yield");
      if (spin == 4000000000UL) { u64 elr; __asm__ volatile ("mrs %0, elr_el1" : "=r" (elr));
        printf ("BKL: CPU %d waits for CPU %d (elr 0x%lx, its task %d, depth %d)\n", c, bkl_owner, elr, pc[bkl_owner].cur_task, pc[bkl_owner].depth); }
    }
  }
  bkl_owner = c;
  pc[c].depth = 1;
  current_task = pc[c].cur_task;                                 // the book's view of "this CPU"
  current_as   = pc[c].cur_as;
  arch_sync_ttbr0 ();                                            // another CPU may have replaced the address space (exec)
}

void bkl_unlock (void) {
  int c = smp_cpu ();
  if (--pc[c].depth > 0) return;
  pc[c].cur_task = current_task;
  pc[c].cur_as   = current_as;
  bkl_owner = -1;
  pc[c].in_kernel = 0;
  __sync_lock_release (&bkl_word);
}

int  bkl_depth (void) { return pc[smp_cpu ()].depth; }
void bkl_set_depth (int d) { pc[smp_cpu ()].depth = d; }

void smp_boot_init (void) {
  u64 m;
  __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (m));
  pc[0].mpidr = m & 0xff00ffffffUL;
  pc[0].online = 1;
  pc[0].cur_task = current_task;
  pc[0].cur_as = current_as;
  bkl_lock ();
}

// wait for an interrupt without holding the kernel lock
void arch_idle (void) {
  bkl_unlock ();
  __asm__ volatile ("msr daifclr, #2; wfi; msr daifset, #2" ::: "memory");
  bkl_lock ();
}

// ---- the scheduler of the book, for several CPUs ----
static thread_id marked[MAX_CPUS];
static int nmarked = 0;

// The book's scheduler walks the ready queue and expects every thread in it to be ready to run (it never gives
// up looking at a thread that is not).  So the threads of other CPUs are taken out of the queue while the
// scheduler chooses, and put back (at the end) afterwards.
void smp_unmark (void) {
  while (nmarked > 0) {
    thread_id t = marked[--nmarked];
    add_to_ready_queue (t);
  }
}

static void smp_mark_others (void) {
  int me = smp_cpu ();
  for (int c = 0; c < MAX_CPUS; c++) {
    if (c == me || !pc[c].online) continue;
    thread_id t = pc[c].cur_task;
    if (t > 1 && thread_table[t].used && thread_table[t].state == TSTATE_READY) {
      int dup = 0;
      for (int k = 0; k < nmarked; k++) if (marked[k] == t) dup = 1;
      if (dup) continue;
      remove_from_ready_queue (t);
      marked[nmarked++] = t;
    }
  }
}

// ulix-port.o defines the book's scheduler as book_scheduler (renamed by the Makefile); all calls land here
void scheduler (context_t *r, int source) {
  if (ncpus_online > 1) smp_mark_others ();
  book_scheduler (r, source);                // (if it switches threads, arch_context_switch unmarks before the switch)
  smp_unmark ();
}

// is the thread running on another CPU?
int smp_running_elsewhere (thread_id t) {
  int me = smp_cpu ();
  for (int c = 0; c < MAX_CPUS; c++) if (c != me && pc[c].online && pc[c].cur_task == t) return 1;
  return 0;
}

// ---- signals for threads on other CPUs ----
static volatile u32 deferred_sig[MAX_THREADS];

int u_kill (int pid, int signo) {            // the book's u_kill is book_u_kill
  if (ncpus_online > 1 && pid > 1 && pid < MAX_THREADS && signo >= 0 && signo <= 31 && thread_table[pid].used) {
    int me = smp_cpu ();
    for (int c = 0; c < MAX_CPUS; c++) {
      if (c != me && pc[c].online && pc[c].cur_task == pid) {
        deferred_sig[pid] |= 1u << signo;
        gic_send_sgi (c, SGI_WAKE);          // makes that CPU enter the kernel, where it ends/stops/signals the thread itself
        return 0;
      }
    }
  }
  return book_u_kill (pid, signo);
}

// called by a CPU that holds the lock, for the thread it runs
void smp_run_deferred (void) {
  thread_id t = current_task;
  while (t > 1 && deferred_sig[t]) {
    u32 bits = deferred_sig[t];
    deferred_sig[t] = 0;
    for (int s = 0; s < 32; s++) if (bits & (1u << s)) book_u_kill (t, s);
    if (thread_table[t].state != TSTATE_READY) return;
  }
}

// make every other CPU leave user mode (it enters the kernel and waits for the lock, which the caller holds)
void smp_stop_others (void) {
  int me = smp_cpu();
  for (int c = 0; c < MAX_CPUS; c++) {
    if (c == me || !pc[c].online) continue;
    u32 e0 = pc[c].entries;
    gic_send_sgi (c, SGI_STOP);
    for (long spin = 0; spin < 100000000 && !pc[c].in_kernel && pc[c].entries == e0; spin++) __asm__ volatile ("yield");
  }
}

// ---- starting the other CPUs ----
static long psci_call_conduit (int hvc, u32 fn, u64 a1, u64 a2, u64 a3) {
  register u64 x0 __asm__ ("x0") = fn;
  register u64 x1 __asm__ ("x1") = a1;
  register u64 x2 __asm__ ("x2") = a2;
  register u64 x3 __asm__ ("x3") = a3;
  if (hvc) __asm__ volatile ("hvc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3) : "memory");
  else                  __asm__ volatile ("smc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3) : "memory");
  return (long)x0;
}

static long psci_call (u32 fn, u64 a1, u64 a2, u64 a3) { return psci_call_conduit (plat_psci_hvc (), fn, a1, a2, a3); }

void secondary_main (int cpu) {
  gic_init_cpu ();
  irq_init_cpu ();                           // this CPU's timer
  pc[cpu].cur_task = 1;                      // idle
  pc[cpu].cur_as = 1;
  __sync_synchronize ();
  pc[cpu].online = 1;
  for (;;) __asm__ volatile ("msr daifclr, #2; wfi");     // all work happens in the interrupt handler
}

void smp_start (void) {
  static int started = 0;
  if (started || !plat_psci () || plat_ncpus () <= 1) return;
  started = 1;
  u64 boot = pc[0].mpidr;
  int n = 1;
  __asm__ volatile ("mrs %0, tcr_el1" : "=r" (secondary_tcr));
  __asm__ volatile ("dc civac, %0; dsb sy" :: "r" (&secondary_tcr) : "memory");
  for (int i = 0; i < plat_ncpus () && n < MAX_CPUS; i++) {
    u64 m = plat_cpu_mpidr (i);
    if (m == boot) continue;
    pc[n].mpidr = m;
    long r = psci_call (plat_psci_cpu_on (), m, (u64)secondary_entry - KERNEL_VA, n);
    if (r != 0) { printf ("SMP: CPU_ON for CPU %d (MPIDR 0x%lx) failed: %ld\n", n, m, r); continue; }
    u64 t0, now, freq;
    __asm__ volatile ("mrs %0, cntpct_el0; mrs %1, cntfrq_el0" : "=r" (t0), "=r" (freq));
    do {
      __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (now));
    } while (!pc[n].online && now - t0 < freq);        // up to one second
    if (pc[n].online) { printf ("SMP: CPU %d online (MPIDR 0x%lx)\n", n, m); ncpus_online++; n++; }
    else printf ("SMP: CPU %d did not start\n", n);
  }
}

// before a CPU returns to user mode: signals that were handed over, and a thread that was ended or stopped meanwhile
extern void arch_signal_check (context_t *f);
void smp_before_return (void *f) {
  smp_run_deferred ();
  arch_signal_check ((context_t *)f);                      // a signal that was handed over is delivered now, not at the next entry
  while (current_task > 1 && thread_table[current_task].state != TSTATE_READY)
    scheduler ((context_t *)f, SCHED_SRC_RESIGN);
}

void smp_count_switch (void) { pc[smp_cpu ()].switches++; }

void smp_print_stats (void) {
  for (int c = 0; c < MAX_CPUS; c++)
    if (pc[c].online) printf ("SMP: CPU %d: %u kernel entries, %u context switches\n", c, pc[c].entries, pc[c].switches);
}
