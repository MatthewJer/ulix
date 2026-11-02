// Ulix ARM64 -- more than one CPU
#ifndef SMP_H
#define SMP_H
#include "plat.h"

int  smp_cpu (void);                       // number of the calling CPU (TPIDR_EL1)
u64  smp_mpidr (int cpu);
void smp_boot_init (void);                 // the boot CPU takes the kernel lock
void smp_start (void);                     // bring the other CPUs up (PSCI), once the scheduler runs
int  smp_online_cpus (void);
void bkl_lock (void);                      // the big kernel lock: the code of the book runs on one CPU at a time
void bkl_unlock (void);
int  bkl_depth (void);
void bkl_set_depth (int d);
void arch_idle (void);                     // give the lock away, wait for an interrupt, take it back
#endif
