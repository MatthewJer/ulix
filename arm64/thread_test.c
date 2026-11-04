// Ulix ARM64 -- test of the book's thread code (scheduler, ready/blocked queues, mutexes and
// kernel semaphores from ulix-book.nw) with kernel threads.

#include "ulix-api.h"

extern void arch_threads_init (void);
extern void arch_idle (void);
extern int  arch_thread_create (void (*entry)(void *), void *arg, const char *name);
extern int  arch_threads_reap (void);
extern void arch_resign (void);
extern int  arch_switch_count;
extern long frames_in_use;

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf ("  ok:   %s\n", msg); \
                              else { printf ("  FAIL: %s\n", msg); fails++; } } while (0)

static int live_threads (void) {
  int n = 0;
  for (thread_id t = 2; t < MAX_THREADS; t++) if (thread_table[t].used) n++;
  return n;
}

// the main thread of the test is thread 1, the idle thread: it waits for the timer interrupt,
// which makes the scheduler switch to the ready worker threads
static void run_until_done (void) {
  while (live_threads () > 0) {
    arch_idle ();
  }
}

// ---- test 1: a mutex protects a counter, although every thread gives up the CPU while holding it ----
static lock counter_lock;
static volatile int counter = 0, in_critical = 0, max_in_critical = 0;
#define ROUNDS 150

static void counter_worker (void *arg) {
  for (int i = 0; i < ROUNDS; i++) {
    mutex_lock (counter_lock);
    in_critical++;
    if (in_critical > max_in_critical) max_in_critical = in_critical;
    int tmp = counter;
    arch_resign ();                      // others run now and must block on the mutex
    counter = tmp + 1;
    in_critical--;
    mutex_unlock (counter_lock);
    arch_resign ();
  }
}

// ---- test 2: bounded buffer with two counting semaphores ----
#define SLOTS 4
#define ITEMS 60
static kl_semaphore_id sem_items, sem_space;
static int ring[SLOTS], head = 0, tail = 0, consumed_sum = 0, consumed_in_order = 1, consumed_n = 0;

static void producer (void *arg) {
  for (int i = 1; i <= ITEMS; i++) {
    wait_semaphore (sem_space);
    ring[head] = i; head = (head + 1) % SLOTS;
    signal_semaphore (sem_items);
  }
}

static void consumer (void *arg) {
  int expect = 1;
  for (int i = 1; i <= ITEMS; i++) {
    wait_semaphore (sem_items);
    int v = ring[tail]; tail = (tail + 1) % SLOTS;
    signal_semaphore (sem_space);
    if (v != expect++) consumed_in_order = 0;
    consumed_sum += v;
    consumed_n++;
  }
}

// ---- test 3: a thread that only finishes after another one signalled it ----
static kl_semaphore_id sem_go;
static volatile int stage = 0;
static void waiter (void *arg)   { stage = 1; wait_semaphore (sem_go); stage = 2; }
static void releaser (void *arg) { while (stage != 1) arch_resign (); arch_resign (); signal_semaphore (sem_go); }

// ---- test 4: processes that read the serial disk at the same time sleep until the receive interrupt has brought the block ----
extern unsigned long plat_uart2 (void);
extern int sd_sleeps, sd_blocks_read;
static volatile int sd_readers_ok = 0;
static void sd_reader (void *arg) {
  static char blk[2][1024];
  int me = (int)(long)arg, good = 1;
  for (int round = 0; round < 4; round++) {
    readblock_serial (me == 0 ? 1 : 1439, blk[me]);
    if (me == 0 && !((byte)blk[0][16] == 0x78 && (byte)blk[0][17] == 0x24)) good = 0;     // Minix superblock
    if (me == 1) for (int i = 0; i < 1024; i++) if (blk[1][i] != (char)(i * 13 + 5)) good = 0;   // what bookfs_test wrote there
  }
  if (good) sd_readers_ok++;
}

int thread_test (void) {
  fails = 0;
  long frames_before = frames_in_use;
  arch_threads_init ();
  CHECK (system_start_time > 1700000000UL && system_time >= system_start_time,
         "the clock starts at a plausible Unix time (after November 2023)");
  CHECK (current_task == 1 && thread_table[0].next == 1 && thread_table[1].next == 0,
         "idle thread 1 is the only entry of the ready queue");

  counter_lock = get_new_lock ("counter");
  CHECK (counter_lock != NULL, "get_new_lock");
  // (interrupts masked: a timer tick between two creates would start the first worker early and make the check flaky)
  __asm__ volatile ("msr daifset, #2" ::: "memory");
  for (int i = 0; i < 3; i++) arch_thread_create (counter_worker, 0, "counter");
  CHECK (live_threads () == 3 && thread_table[1].next == 2, "3 worker threads are in the ready queue");
  int switches0 = arch_switch_count;
  __asm__ volatile ("msr daifclr, #2" ::: "memory");
  run_until_done ();
  CHECK (counter == 3 * ROUNDS, "mutex: counter is exactly 3 * 150 although threads yield inside the critical section");
  CHECK (max_in_critical == 1, "mutex: never more than one thread inside the critical section");
  CHECK (arch_switch_count - switches0 > 3 * ROUNDS, "many context switches happened (workers switched among each other)");
  printf ("  info: %d context switches\n", arch_switch_count - switches0);
  CHECK (thread_table[1].next == 0 && thread_table[0].next == 1, "ready queue is back to just the idle thread");

  sem_items = get_new_semaphore (0);
  sem_space = get_new_semaphore (SLOTS);
  arch_thread_create (consumer, 0, "consumer");           // consumer first: it must block at once
  arch_thread_create (producer, 0, "producer");
  run_until_done ();
  CHECK (consumed_n == ITEMS && consumed_sum == ITEMS * (ITEMS + 1) / 2, "semaphores: 60 items passed through a 4-slot buffer, sum correct");
  CHECK (consumed_in_order, "semaphores: items arrived in order");

  sem_go = get_new_semaphore (0);
  arch_thread_create (waiter, 0, "waiter");
  arch_thread_create (releaser, 0, "releaser");
  run_until_done ();
  CHECK (stage == 2, "a blocked thread continues after the other signalled its semaphore");

  CHECK (live_threads () == 0, "all threads finished, zombies removed from the thread table");

  if (plat_uart2 ()) {
    int slept = sd_sleeps, reads = sd_blocks_read;
    arch_thread_create (sd_reader, (void *)0L, "sdisk0");
    arch_thread_create (sd_reader, (void *)1L, "sdisk1");
    run_until_done ();
    CHECK (sd_readers_ok == 2 && sd_blocks_read - reads == 8, "serial disk: two threads read 4 blocks each at the same time, all data correct");
    CHECK (sd_sleeps > slept, "serial disk: the threads slept until the receive interrupt delivered the block");
  }
  arch_threads_reap ();                                    // stacks of IDs that were not reused
  CHECK (frames_in_use == frames_before, "no frame leaked");
  return fails;
}
