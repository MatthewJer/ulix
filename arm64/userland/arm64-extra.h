// What the AArch64 build adds to programs of lib-build/tools that were written for a 64 MByte PC.
// (Only the programs that need it include this file; see the rule for free.c in the Makefile.)
#ifndef ARM64_EXTRA_H
#define ARM64_EXTRA_H
#define __NR_total_frames 601                       // AArch64-only system call: frames (4 KByte) of RAM
static inline int total_frames (void) { return syscall1 (__NR_total_frames); }

// the network: raw ICMP sockets (net_syscalls.c). The structure is passed by address; user addresses are below 2 GByte,
// so they fit into the int that the system call wrappers take.
#define __NR_icmp_open  602
#define __NR_icmp_send  603
#define __NR_icmp_recv  604
#define __NR_icmp_close 605
#define __NR_clock_us   606
struct icmp_msg { int handle; unsigned int ip; char *buf; int len; int timeout_ms; };
static inline int icmp_open (void)                  { return syscall1 (__NR_icmp_open); }
static inline int icmp_send (struct icmp_msg *m)    { return syscall2 (__NR_icmp_send, (int)(long)m); }
static inline int icmp_recv (struct icmp_msg *m)    { return syscall2 (__NR_icmp_recv, (int)(long)m); }
static inline int icmp_close (int handle)           { return syscall2 (__NR_icmp_close, handle); }
static inline unsigned int clock_us (void)          { return (unsigned int)syscall1 (__NR_clock_us); }

// the settings of the network interface (the programs ifconfig, route and dhclient)
#define __NR_net_info 607
#define __NR_net_set  608
#define __NR_net_dhcp 609
struct net_info { char name[16]; unsigned char mac[6]; unsigned short mtu; unsigned flags; unsigned address, netmask, gateway, dns, dhcp_server, dhcp_lease; };
struct net_set  { unsigned what; unsigned address, netmask, gateway; };         // what: 1 = address and netmask, 2 = router, 4 = remove the router
struct net_dhcp_args { int timeout_ms; unsigned server, address, netmask, gateway, dns, lease; };
static inline int net_info (struct net_info *i)       { return syscall2 (__NR_net_info, (int)(long)i); }          // 0 = ok, -1 = no network
static inline int net_set (struct net_set *s)         { return syscall2 (__NR_net_set, (int)(long)s); }           // -2 = only root may do this
static inline int net_dhcp (struct net_dhcp_args *d)  { return syscall2 (__NR_net_dhcp, (int)(long)d); }
#endif
