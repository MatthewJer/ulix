// Ulix ARM64 -- system calls for the network (AArch64 only, numbers 602 and up; the programs use them through userland/arm64-extra.h)
//
// Programs get raw ICMP sockets: they build ICMP messages themselves, the kernel puts them into IP packets (net.c). That is all
// that "ping" needs. TCP and UDP sockets for programs come later (then telnet can be a program, too).
//
//     icmp_open ()           a socket number, or -1
//     icmp_send (&m)         m.handle, m.ip, m.buf, m.len: send the ICMP message in m.buf to m.ip (0 = sent)
//     icmp_recv (&m)         m.handle, m.buf, m.len (room), m.timeout_ms: wait for a message; m.ip = sender; returns its length, 0 = timeout
//     icmp_close (handle)
//     clock_us ()            microseconds since the start of the machine (wraps after 71 minutes; programs subtract two values)
//     net_info (&i)          the interface: name, Ethernet address, IP address, netmask, router, name server (ifconfig, route)
//     net_set (&s)           change address and netmask, or the router (only root; ifconfig, route)
//     net_dhcp (&d)          ask a DHCP server and use what it says (only root; dhclient)
//
// A program that waits for an answer gives the CPU to the others in between (arch_resign), and looks into the network card each
// time it gets the CPU back: for now there is no thread that does this for everybody (that needs an interrupt of the card).

#include "ulix-api.h"

extern int  icmp_open (int owner, int (*alive) (int owner));
extern void icmp_close (int h);
extern int  icmp_send (int h, unsigned ip, const unsigned char *msg, int len);
extern int  icmp_get (int h, unsigned char *buf, int max, unsigned *from);
extern int  net_poll_once (void);
extern unsigned long long net_now_ms (void);
extern void arch_resign (void);

struct icmp_msg { int handle; unsigned int ip; char *buf; int len; int timeout_ms; };       // the same as in userland/arm64-extra.h

// The interface and its settings (the same structures are in userland/arm64-extra.h).
struct net_info { char name[16]; unsigned char mac[6]; unsigned short mtu; unsigned flags; unsigned address, netmask, gateway, dns, dhcp_server, dhcp_lease; };
struct net_set  { unsigned what; unsigned address, netmask, gateway; };         // what: 1 = address and netmask, 2 = router, 4 = remove the router
struct net_dhcp_args { int timeout_ms; unsigned server, address, netmask, gateway, dns, lease; };
struct dhcp_result { unsigned server, address, netmask, gateway, dns, lease_seconds; };
extern unsigned net_my_ip, net_netmask, net_gateway, net_dns;
extern const char *net_if_name;
extern const unsigned char *net_dev_mac (void);
extern int dhcp_run (int timeout_ms, void (*idle) (void));
extern struct dhcp_result dhcp_last;

static int alive (int tid) { return tid > 0 && tid < MAX_THREADS && thread_table[tid].used; }

void syscall_icmp_open (context_t *r)  { eax_return (icmp_open (current_task, alive)); }
void syscall_icmp_close (context_t *r) { icmp_close ((int)r->ebx); r->eax = 0; }

void syscall_icmp_send (context_t *r) {
  struct icmp_msg *m = (struct icmp_msg *)r->ebx;
  eax_return (icmp_send (m->handle, m->ip, (unsigned char *)m->buf, m->len));
}

void syscall_icmp_recv (context_t *r) {
  struct icmp_msg *m = (struct icmp_msg *)r->ebx;
  unsigned long long start = net_now_ms ();
  for (;;) {
    unsigned from = 0;
    int n = icmp_get (m->handle, (unsigned char *)m->buf, m->len, &from);
    if (n != 0) { m->ip = from; eax_return (n); }                    // a message (or -1: no such socket)
    if (net_now_ms () - start >= (unsigned)m->timeout_ms) eax_return (0);
    net_poll_once ();
    arch_resign ();
  }
}

void syscall_clock_us (context_t *r) {
  unsigned long long freq, count;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (count));
  eax_return ((unsigned)(count * 1000000 / freq));
}

// ---- switching off (not about the network, but the system calls of the port are kept together here) ----
extern void smp_poweroff (void);
extern int  smp_reboot (void);

// First everything that is only in the buffer cache goes to the disks (what the system call sync does), then the machine is switched
// off by the firmware. Under qemu that ends qemu. A machine without that function (the Raspberry Pi: no PSCI) stops here instead.
void syscall_poweroff (context_t *r) {
  buffer_sync (1);
  printf ("System halted.\n");
  smp_poweroff ();
  for (;;) __asm__ volatile ("msr daifset, #3; wfi");           // (no interrupts, wait forever; the power button is the way out)
}

// The same, but the firmware starts the machine again (PSCI SYSTEM_RESET): the kernel boots from the beginning. -1 if that is not possible.
void syscall_reboot (context_t *r) {
  buffer_sync (1);
  printf ("Rebooting ...\n");
  eax_return (smp_reboot ());
}

// ---- the configuration. Changing it is for root only, as on the BSDs (euid 0). -2 = not allowed ----
static int is_root (void) { return thread_table[current_task].euid == 0; }

void syscall_net_info (context_t *r) {
  struct net_info *i = (struct net_info *)r->ebx;
  for (int k = 0; k < 16; k++) i->name[k] = net_if_name[k] ? net_if_name[k] : 0;
  const unsigned char *mac = net_dev_mac ();
  for (int k = 0; k < 6; k++) i->mac[k] = mac[k];
  i->mtu = 1500;  i->flags = 1;
  i->address = net_my_ip;  i->netmask = net_netmask;  i->gateway = net_gateway;  i->dns = net_dns;
  i->dhcp_server = dhcp_last.server;  i->dhcp_lease = dhcp_last.lease_seconds;
  r->eax = 0;
}

void syscall_net_set (context_t *r) {
  struct net_set *s = (struct net_set *)r->ebx;
  if (!is_root ()) eax_return (-2);
  if (s->what & 1) { net_my_ip = s->address;  net_netmask = s->netmask; }
  if (s->what & 2) net_gateway = s->gateway;
  if (s->what & 4) net_gateway = 0;
  r->eax = 0;
}

void syscall_net_dhcp (context_t *r) {
  struct net_dhcp_args *d = (struct net_dhcp_args *)r->ebx;
  if (!is_root ()) eax_return (-2);
  int result = dhcp_run (d->timeout_ms, arch_resign);                  // (the program that waits gives the CPU to the others)
  d->server = dhcp_last.server;  d->address = dhcp_last.address;  d->netmask = dhcp_last.netmask;
  d->gateway = dhcp_last.gateway;  d->dns = dhcp_last.dns;  d->lease = dhcp_last.lease_seconds;
  eax_return (result);
}
