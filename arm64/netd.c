// Ulix ARM64 -- the network thread
//
// Frames arrive at any time, and somebody has to look at them: answer ARP and ping, hand data to TCP, run the timers of TCP. This
// is the job of a kernel thread, "netd". It does the same as the disk driver of the book: it sleeps in a blocked queue, and the
// interrupt of the network card wakes it up. It also wakes up by itself twenty times a second (from the timer interrupt): TCP needs
// that to send data again that was not acknowledged, and a card without an interrupt number in the device tree is served this way, too.
//
// (The queue and the state of the floppy disk are used for the sleeping: this machine has no floppy, and the thread then shows up
// in "ps" with a state the book knows.)

#include "ulix-api.h"

extern int  net_poll_once (void);
extern int  net_dev_start_irq (void);
extern void net_dev_ack (void);
extern int  echo_start (void);
extern int  echo_step (void);
extern int  net_echo;
extern void gic_enable (int irq);
extern void arch_resign (void);
extern int  arch_thread_create_high (void (*entry) (void *), void *arg, const char *name);

int netd_irq = 0;
static int running = 0, ticks_seen = 0;

static void wake (void) {
  thread_id t = front_of_blocked_queue (floppy_queue);
  if (t) deblock (t, &floppy_queue);
}

void netd_interrupt (void) {                  // the card has something: say so to the card, wake the thread
  net_dev_ack ();
  wake ();
}

void netd_tick (void) {                       // every 10 ms; the thread gets a nudge every 50 ms
  if (running && ++ticks_seen >= 5) { ticks_seen = 0; wake (); }
}

static void netd (void *arg) {
  for (;;) {
    int moved;
    do { moved = net_poll_once (); if (net_echo) moved |= echo_step (); } while (moved);       // everything that is there
    block (&floppy_queue, TSTATE_WAITFLP);                                                    // then sleep until we are woken
    arch_resign ();
  }
}

void netd_start (void) {
  netd_irq = net_dev_start_irq ();
  if (netd_irq) gic_enable (netd_irq);
  if (net_echo && echo_start () != 0) printf ("netd: cannot open port 7\n");
  arch_thread_create_high (netd, 0, "netd");                       // (a high thread number: see arch_thread_create_high)
  running = 1;
  printf ("netd: network thread started (%s)%s\n", netd_irq ? "woken by the interrupt of the card" : "polling every 50 ms", net_echo ? ", echo service on port 7" : "");
}
