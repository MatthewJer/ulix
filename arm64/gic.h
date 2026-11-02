// Ulix ARM64 -- interrupt controller (GICv2 and GICv3)
#ifndef GIC_H
#define GIC_H
#include "plat.h"

#define SGI_STOP    1                      // software generated interrupts used by the kernel
#define SGI_WAKE    2

void gic_init (void);                      // boot CPU: distributor and the interface of this CPU
void gic_init_cpu (void);                  // every other CPU, for itself
void gic_enable (int irq);                 // enable an interrupt (a PPI for the calling CPU, an SPI is sent to CPU 0)
u32  gic_ack (void);                       // acknowledge: returns the raw value, GIC_INTID () is the interrupt number
void gic_eoi (u32 iar);                    // end of interrupt (pass the value of gic_ack)
void gic_send_sgi (int cpu, int sgi);      // software interrupt to the CPU with this number
void gic_route (int irq, int cpu);         // deliver a shared peripheral interrupt (SPI) to this CPU
#define GIC_INTID(iar)  ((iar) & 0x3ff)
#endif
