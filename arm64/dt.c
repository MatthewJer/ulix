// Ulix ARM64 -- flattened device tree reader
// Reads the device tree that the boot loader (qemu) hands over and fills struct platform with the values the
// kernel needs: memory, interrupt controller, console, clock, timer, CPUs, PSCI and virtio devices.
// Without a device tree the defaults of the qemu "virt" machine are used.

#include "plat.h"

struct platform plat = {
  .dt_ok = 0,
  .ram_base = 0x40000000UL, .ram_size = 128UL << 20,
  .gic_version = 0, .gicd = 0x08000000UL, .gicc = 0x08010000UL, .gicr = 0x080a0000UL, .gicr_size = 0xf60000UL,
  .uart = 0x09000000UL, .uart_irq = 33, .rtc = 0x09010000UL, .timer_irq = 30,
  .ncpus = 1, .psci = 0, .psci_hvc = 0, .psci_cpu_on = 0xc4000003U,
  .nvirtio = 0, .virtio_coherent = 0,
};

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE   2
#define FDT_PROP       3
#define FDT_NOP        4
#define FDT_END        9

static u32 be32 (const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u64 cells (const u8 *p, int n) { u64 v = 0; for (int i = 0; i < n; i++) v = (v << 32) | be32 (p + 4 * i); return v; }

static int str_eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int str_starts (const char *a, const char *prefix) { while (*prefix) if (*a++ != *prefix++) return 0; return 1; }

struct node {
  const char *name;
  int pac, psc;                  // address and size cells for this node's "reg" (from the parent)
  int ac, sc;                    // #address-cells, #size-cells for the children
  const u8 *reg;  int reglen;
  const u8 *compat;  int clen;
  const u8 *irq;  int irqlen;
  const char *devtype, *method;
  int coherent, has_cpu_on;  u32 cpu_on;
};

// does the "compatible" string list contain a string that starts with prefix (or equals it)?
static int compat_has (const struct node *n, const char *s, int exact) {
  for (int i = 0; i < n->clen; ) {
    const char *c = (const char *)n->compat + i;
    if (exact ? str_eq (c, s) : str_starts (c, s)) return 1;
    while (i < n->clen && n->compat[i]) i++;
    i++;
  }
  return 0;
}

static int irq_number (const u8 *p) {                // three cells: type (0 = SPI, 1 = PPI), number, flags
  u32 type = be32 (p), num = be32 (p + 4);
  return type == 0 ? (int)num + 32 : (int)num + 16;
}

static void classify (const struct node *n) {
  if (n->devtype && str_eq (n->devtype, "memory") && n->reg && n->reglen >= 4 * (n->pac + n->psc)) {
    plat.ram_base = cells (n->reg, n->pac);
    plat.ram_size = cells (n->reg + 4 * n->pac, n->psc);
    return;
  }
  if (n->devtype && str_eq (n->devtype, "cpu") && n->reg && plat.ncpus < MAX_CPUS + 1) {
    static int ncpu_nodes = 0;
    if (ncpu_nodes < MAX_CPUS) plat.cpu_mpidr[ncpu_nodes++] = cells (n->reg, n->pac) & 0xff00ffffffUL;
    plat.ncpus = ncpu_nodes;
    return;
  }
  if (!n->compat) return;
  int entry = 4 * (n->pac + n->psc);
  if (compat_has (n, "arm,gic-v3", 1) && n->reg && n->reglen >= 2 * entry) {
    plat.gic_version = 3;
    plat.gicd = cells (n->reg, n->pac);
    plat.gicr = cells (n->reg + entry, n->pac);
    plat.gicr_size = cells (n->reg + entry + 4 * n->pac, n->psc);
  } else if ((compat_has (n, "arm,cortex-a15-gic", 1) || compat_has (n, "arm,gic-400", 1) ||
              compat_has (n, "arm,cortex-a9-gic", 1) || compat_has (n, "arm,cortex-a7-gic", 1)) &&
             n->reg && n->reglen >= 2 * entry) {
    plat.gic_version = 2;
    plat.gicd = cells (n->reg, n->pac);
    plat.gicc = cells (n->reg + entry, n->pac);
  } else if (compat_has (n, "arm,pl011", 1) && n->reg) {
    static int uarts = 0;                                     // the first PL011 is the console, the second one the serial disk
    if (uarts == 0) {
      plat.uart = cells (n->reg, n->pac);
      if (n->irq && n->irqlen >= 12) plat.uart_irq = irq_number (n->irq);
    } else if (uarts == 1) {
      plat.uart2 = cells (n->reg, n->pac);
      plat.uart2_irq = (n->irq && n->irqlen >= 12) ? irq_number (n->irq) : 0;
    }
    uarts++;
  } else if (compat_has (n, "arm,pl031", 1) && n->reg) {
    plat.rtc = cells (n->reg, n->pac);
  } else if (compat_has (n, "virtio,mmio", 1) && n->reg) {
    if (plat.nvirtio < MAX_VIRTIO) {
      plat.virtio_irq[plat.nvirtio] = (n->irq && n->irqlen >= 12) ? irq_number (n->irq) : 0;
      plat.virtio[plat.nvirtio++] = cells (n->reg, n->pac);
    }
    if (!n->coherent) plat.virtio_coherent = 0;
  } else if (compat_has (n, "arm,psci", 0)) {
    plat.psci = 1;
    plat.psci_hvc = n->method && str_eq (n->method, "hvc");
    if (n->has_cpu_on) plat.psci_cpu_on = n->cpu_on;
  } else if (compat_has (n, "arm,armv8-timer", 1) && n->irq && n->irqlen >= 24) {
    plat.timer_irq = irq_number (n->irq + 12);       // second entry: non-secure physical timer
  }
}

void dt_init (u64 blob_pa) {
  const u8 *h = KVA (blob_pa);
  if (be32 (h) != 0xd00dfeed) {                      // not where the boot loader said: qemu puts it at the start of RAM
    h = KVA (0x40000000UL);
    if (be32 (h) != 0xd00dfeed) return;
  }
  u32 total = be32 (h + 4), off_struct = be32 (h + 8), off_strings = be32 (h + 12);
  if (total > (1 << 20) || off_struct >= total || off_strings >= total) return;
  const u8 *p = h + off_struct;
  const char *strings = (const char *)h + off_strings;

  struct node stack[12];
  int depth = -1;
  plat.virtio_coherent = 1;                          // cleared by the first virtio node without "dma-coherent"
  plat.ncpus = 0;
  plat.rtc = 0;                                      // a board without a PL031 (the Raspberry Pi) has no clock: arch_clock.c copes
  for (;;) {
    u32 tok = be32 (p);  p += 4;
    if (tok == FDT_BEGIN_NODE) {
      const char *name = (const char *)p;
      int len = 0; while (name[len]) len++;
      p += (len + 1 + 3) & ~3;
      if (depth + 1 >= 12) return;
      depth++;
      struct node *n = &stack[depth];
      *n = (struct node){ 0 };
      n->name = name;
      n->pac = depth > 0 ? stack[depth - 1].ac : 2;
      n->psc = depth > 0 ? stack[depth - 1].sc : 1;
      n->ac = 2; n->sc = 1;
    } else if (tok == FDT_PROP) {
      u32 len = be32 (p), nameoff = be32 (p + 4);
      const u8 *val = p + 8;
      p += 8 + ((len + 3) & ~3);
      if (depth < 0) return;
      struct node *n = &stack[depth];
      const char *pn = strings + nameoff;
      if      (str_eq (pn, "reg"))             { n->reg = val; n->reglen = len; }
      else if (str_eq (pn, "compatible"))      { n->compat = val; n->clen = len; }
      else if (str_eq (pn, "interrupts"))      { n->irq = val; n->irqlen = len; }
      else if (str_eq (pn, "device_type"))     n->devtype = (const char *)val;
      else if (str_eq (pn, "method"))          n->method = (const char *)val;
      else if (str_eq (pn, "dma-coherent"))    n->coherent = 1;
      else if (str_eq (pn, "cpu_on"))          { n->cpu_on = be32 (val); n->has_cpu_on = 1; }
      else if (str_eq (pn, "#address-cells"))  n->ac = be32 (val);
      else if (str_eq (pn, "#size-cells"))     n->sc = be32 (val);
    } else if (tok == FDT_END_NODE) {
      if (depth < 0) return;
      classify (&stack[depth]);
      depth--;
    } else if (tok == FDT_END) {
      break;
    } else if (tok != FDT_NOP) {
      return;                                        // damaged
    }
  }
  plat.dt_ok = 1;
  if (plat.ncpus == 0) plat.ncpus = 1;
  if (plat.nvirtio == 0) plat.virtio_coherent = 0;
}

// accessors for files that do not include plat.h (they use the book's headers)
int  plat_psci (void)          { return plat.psci; }
int  plat_psci_hvc (void)      { return plat.psci_hvc; }
unsigned plat_psci_cpu_on (void) { return plat.psci_cpu_on; }
int  plat_ncpus (void)         { return plat.ncpus; }
unsigned long plat_cpu_mpidr (int i) { return plat.cpu_mpidr[i]; }
unsigned long plat_rtc (void)  { return plat.rtc; }
unsigned long plat_ram_size (void) { return plat.ram_size; }
int  plat_uart_irq (void)      { return plat.uart_irq; }
unsigned long plat_uart2 (void) { return plat.uart2; }
int  plat_uart2_irq (void)     { return plat.uart2_irq; }
unsigned long arch_ram_base (void) { return plat.ram_base; }
unsigned long arch_ram_size (void) { return plat.ram_size; }
