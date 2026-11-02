// Threads in user mode: three threads and a mutex, shared memory, trylock, and exit() with a running thread.
#include "ulib.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf ("  user ok:   %s\n", msg); \
                           else { printf ("  user FAIL: %s\n", msg); fails++; } } while (0)

#define ROUNDS 200
static int mutex, counter = 0, in_critical = 0, overlap = 0;
static volatile int finished = 0, tids[3], started = 0;
static volatile int spinner_alive = 0;

static void worker (void) {
  int me = started++;
  tids[me] = gettid ();
  for (int i = 0; i < ROUNDS; i++) {
    mutex_lock (&mutex);
    if (++in_critical != 1) overlap = 1;
    int tmp = counter;
    yield ();                           // other threads run now and must wait for the mutex
    counter = tmp + 1;
    in_critical--;
    mutex_unlock (&mutex);
  }
  finished++;
  pthread_exit ();
}

// outlives the main thread: exit() of the main thread does not end the other threads (as on the x86); the
// address space is freed when the last thread has gone
static void spinner (void) {
  spinner_alive = 1;
  for (int i = 0; i < 300; i++) yield ();
  printf ("  user ok:   a thread goes on after exit() of the main thread\n");
  pthread_exit ();
}

int main (int argc, char **argv) {
  int mypid = getpid ();
  CHECK (mutex_init (&mutex) == 0 && mutex > 0, "pthread_mutex_init gives a handle");
  CHECK (mutex_trylock (&mutex) == 0, "trylock on a free mutex succeeds");
  CHECK (mutex_trylock (&mutex) == -1, "trylock on a locked mutex fails (EBUSY)");
  mutex_unlock (&mutex);

  for (int i = 0; i < 3; i++) CHECK (pthread_create (worker) > 0, "pthread_create");
  while (finished < 3) yield ();

  CHECK (counter == 3 * ROUNDS, "counter is exactly 3 * 200: the mutex serializes the threads");
  CHECK (!overlap, "never two threads in the critical section");
  CHECK (tids[0] != tids[1] && tids[1] != tids[2] && tids[0] != tids[2], "the threads have different thread IDs");
  CHECK (tids[0] != mypid && getpid () == mypid, "thread IDs differ from the process ID, getpid is unchanged");
  CHECK (mutex_destroy (&mutex) == 0 && mutex_lock (&mutex) == -1, "a destroyed mutex is refused");

  // exit() of the main thread while another thread is running
  CHECK (pthread_create (spinner) > 0, "start a thread that outlives the main thread");
  while (!spinner_alive) yield ();
  CHECK (fails == 0, "all checks passed, calling exit() while a thread runs");
  exit (fails);
}
