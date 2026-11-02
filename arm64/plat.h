// Ulix ARM64 -- what the device tree says about the machine (see dt.c)
#ifndef PLAT_H
#define PLAT_H
#include "ulix.h"

#define MAX_CPUS    8
#define MAX_VIRTIO  32

struct platform {
  int dt_ok;                     // 1: values come from a device tree, 0: defaults of the qemu "virt" machine
  u64 ram_base, ram_size;
  int gic_version;               // 2 or 3
  u64 gicd, gicc, gicr, gicr_size;
  u64 uart;  int uart_irq;       // PL011 and its interrupt number (INTID)
  u64 rtc;                       // PL031
  int timer_irq;                 // INTID of the EL1 physical timer (a PPI)
  int ncpus;  u64 cpu_mpidr[MAX_CPUS];
  int psci_hvc;  u32 psci_cpu_on;  int psci;      // PSCI present, conduit (0 = smc, 1 = hvc), function ID of CPU_ON
  int nvirtio;  u64 virtio[MAX_VIRTIO];           // base addresses of the virtio-mmio devices
  int virtio_irq[MAX_VIRTIO];                     // and their interrupt numbers
  int virtio_coherent;                            // all devices say "dma-coherent"
};
extern struct platform plat;
extern u64 boot_dtb;

void dt_init (u64 blob_pa);
#endif
