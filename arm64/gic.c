// Ulix ARM64 -- interrupt controller: GICv2 (memory mapped CPU interface) and GICv3 (system registers,
// redistributors).  The version comes from the device tree (plat.gic_version); without one it is read from the
// distributor.  Replaces the 8259 PIC of the x86 version.

#include "gic.h"

extern u64 smp_mpidr (int cpu);            // MPIDR of a CPU number (smp.c)
extern int smp_cpu (void);                 // number of the calling CPU

static u64 gicd_va, gicc_va, gicr_va;
static u64 rd_va[MAX_CPUS];                // redistributor frame of each CPU (GICv3)
static int version;

#define R32(a)      (*(volatile u32 *)(a))
#define W32(a, v)   (*(volatile u32 *)(a) = (v))

// GICD register offsets (both versions)
#define D_CTLR          0x000
#define D_TYPER         0x004
#define D_IGROUPR(n)    (0x080 + 4 * (n))
#define D_ISENABLER(n)  (0x100 + 4 * (n))
#define D_ICENABLER(n)  (0x180 + 4 * (n))
#define D_IPRIORITYR(n) (0x400 + 4 * (n))
#define D_ITARGETSR(n)  (0x800 + 4 * (n))      // GICv2
#define D_ICFGR(n)      (0xc00 + 4 * (n))
#define D_SGIR          0xf00                  // GICv2
#define D_PIDR2         0xfe8
#define D_IROUTER(n)    (0x6000 + 8 * (n))     // GICv3
// GICv2 CPU interface
#define C_CTLR          0x000
#define C_PMR           0x004
#define C_IAR           0x00c
#define C_EOIR          0x010
// GICv3 redistributor: RD frame and SGI frame (64 KByte further)
#define R_WAKER         0x014
#define R_TYPER         0x008
#define S_BASE          0x10000

static void dsb_isb (void) { __asm__ volatile ("dsb sy; isb" ::: "memory"); }

// ---- GICv3 CPU interface system registers ----
#define ICC_SRE_EL1     "S3_0_C12_C12_5"
#define ICC_IAR1_EL1    "S3_0_C12_C12_0"
#define ICC_EOIR1_EL1   "S3_0_C12_C12_1"
#define ICC_PMR_EL1     "S3_0_C4_C6_0"
#define ICC_BPR1_EL1    "S3_0_C12_C12_3"
#define ICC_IGRPEN1_EL1 "S3_0_C12_C12_7"
#define ICC_SGI1R_EL1   "S3_0_C12_C11_5"

static void d_wait_rwp (void) { while (R32 (gicd_va + D_CTLR) & (1u << 31)) ; }

static u64 mpidr_affinity (u64 mpidr) { return (mpidr & 0xffffff) | ((mpidr >> 32 & 0xff) << 32); }

static void v3_find_redistributor (void) {
  u64 mpidr;
  __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (mpidr));
  u64 want = mpidr_affinity (mpidr), p = gicr_va;
  for (u64 off = 0; off < plat.gicr_size; ) {
    u64 typer = *(volatile u64 *)(p + off + R_TYPER);
    if (((typer >> 32) & 0xffffffff) == ((want & 0xffffff) | ((want >> 32) << 24))) { rd_va[smp_cpu ()] = p + off; return; }
    if (typer & (1 << 4)) break;                         // last redistributor
    off += (typer & (1 << 1)) ? 0x40000 : 0x20000;       // with virtual LPI frames each one is larger
  }
  printf ("gic: no redistributor for CPU %d\n", smp_cpu ());
}

static void v3_cpu_init (void) {
  v3_find_redistributor ();
  u64 rd = rd_va[smp_cpu ()];
  if (!rd) return;
  u32 waker = R32 (rd + R_WAKER);                        // wake the redistributor up
  W32 (rd + R_WAKER, waker & ~(1u << 1));
  while (R32 (rd + R_WAKER) & (1u << 2)) ;
  u64 sgi = rd + S_BASE;
  W32 (sgi + D_IGROUPR (0), 0xffffffff);                 // group 1
  W32 (sgi + D_ICENABLER (0), 0xffffffff);
  for (int i = 0; i < 8; i++) W32 (sgi + D_IPRIORITYR (i), 0xa0a0a0a0);
  W32 (sgi + D_ISENABLER (0), 0xffff);                   // SGIs 0..15 on
  dsb_isb ();
  u64 sre;
  __asm__ volatile ("mrs %0, " ICC_SRE_EL1 : "=r" (sre));
  sre |= 1;                                              // system register interface
  __asm__ volatile ("msr " ICC_SRE_EL1 ", %0; isb" :: "r" (sre));
  __asm__ volatile ("msr " ICC_PMR_EL1 ", %0" :: "r" (0xffUL));
  __asm__ volatile ("msr " ICC_BPR1_EL1 ", %0" :: "r" (0UL));
  __asm__ volatile ("msr " ICC_IGRPEN1_EL1 ", %0; isb" :: "r" (1UL));
}

static void v2_cpu_init (void) {
  W32 (gicc_va + C_PMR, 0xff);
  W32 (gicc_va + C_CTLR, 1);
  W32 (gicd_va + D_ISENABLER (0), 0xffff);               // SGIs (banked per CPU)
  for (int i = 0; i < 8; i++) W32 (gicd_va + D_IPRIORITYR (i), 0xa0a0a0a0);
}

void gic_init (void) {
  gicd_va = KERNEL_VA + plat.gicd;
  gicc_va = KERNEL_VA + plat.gicc;
  gicr_va = KERNEL_VA + plat.gicr;
  version = plat.gic_version;
  if (!version) {                                        // no device tree: ask the distributor (ArchRev in GICD_PIDR2)
    u32 rev = (R32 (gicd_va + D_PIDR2) >> 4) & 15;
    version = rev >= 3 ? 3 : 2;
  }
  u32 lines = ((R32 (gicd_va + D_TYPER) & 31) + 1) * 32;
  if (lines > 1020) lines = 1020;
  if (version == 3) {
    W32 (gicd_va + D_CTLR, 0);
    d_wait_rwp ();
    for (u32 i = 1; i < lines / 32; i++) { W32 (gicd_va + D_IGROUPR (i), 0xffffffff); W32 (gicd_va + D_ICENABLER (i), 0xffffffff); }
    for (u32 i = 8; i < lines / 4; i++) W32 (gicd_va + D_IPRIORITYR (i), 0xa0a0a0a0);
    W32 (gicd_va + D_CTLR, (1 << 4) | (1 << 1) | 1);     // ARE, enable group 1 (written like Linux does)
    d_wait_rwp ();
    v3_cpu_init ();
  } else {
    W32 (gicd_va + D_CTLR, 1);
    v2_cpu_init ();
  }
  dsb_isb ();
  printf ("GIC: version %d%s, distributor at 0x%lx, %u interrupt lines\n", version, plat.gic_version ? "" : " (detected)", plat.gicd, lines);
}

void gic_init_cpu (void) {
  if (version == 3) v3_cpu_init (); else v2_cpu_init ();
  dsb_isb ();
}

void gic_enable (int irq) {
  if (irq < 32) {                                        // SGI/PPI: the registers of the calling CPU
    if (version == 3) {
      u64 rd = rd_va[smp_cpu ()];
      if (rd) { u64 sgi = rd + S_BASE; W32 (sgi + D_IPRIORITYR (irq / 4), (R32 (sgi + D_IPRIORITYR (irq / 4)) & ~(0xffu << (irq % 4 * 8))) | (0x80u << (irq % 4 * 8))); W32 (sgi + D_ISENABLER (0), 1u << irq); }
    } else {
      W32 (gicd_va + D_IPRIORITYR (irq / 4), R32 (gicd_va + D_IPRIORITYR (irq / 4)) & ~(0xffu << (irq % 4 * 8)));
      W32 (gicd_va + D_ISENABLER (0), 1u << irq);
    }
  } else {                                               // SPI: goes to CPU 0
    W32 (gicd_va + D_IPRIORITYR (irq / 4), R32 (gicd_va + D_IPRIORITYR (irq / 4)) & ~(0xffu << (irq % 4 * 8)));
    if (version == 3) {
      *(volatile u64 *)(gicd_va + D_IROUTER (irq)) = mpidr_affinity (smp_mpidr (0));
    } else {
      W32 (gicd_va + D_ITARGETSR (irq / 4), (R32 (gicd_va + D_ITARGETSR (irq / 4)) & ~(0xffu << (irq % 4 * 8))) | (1u << (irq % 4 * 8)));
    }
    W32 (gicd_va + D_ISENABLER (irq / 32), 1u << (irq % 32));
  }
  dsb_isb ();
}

void gic_route (int irq, int cpu) {
  if (irq < 32) return;
  if (version == 3) {
    *(volatile u64 *)(gicd_va + D_IROUTER (irq)) = mpidr_affinity (smp_mpidr (cpu));
  } else {
    u32 v = R32 (gicd_va + D_ITARGETSR (irq / 4)) & ~(0xffu << (irq % 4 * 8));
    W32 (gicd_va + D_ITARGETSR (irq / 4), v | ((1u << cpu) << (irq % 4 * 8)));
  }
  dsb_isb ();
}

u32 gic_ack (void) {
  if (version == 3) { u64 v; __asm__ volatile ("mrs %0, " ICC_IAR1_EL1 : "=r" (v)); return (u32)v; }
  return R32 (gicc_va + C_IAR);
}

void gic_eoi (u32 iar) {
  if (version == 3) __asm__ volatile ("msr " ICC_EOIR1_EL1 ", %0; isb" :: "r" ((u64)iar));
  else              W32 (gicc_va + C_EOIR, iar);
}

void gic_send_sgi (int cpu, int sgi) {
  if (version == 3) {
    u64 m = smp_mpidr (cpu);
    u64 v = (1UL << (m & 0xf)) | ((m >> 8 & 0xff) << 16) | ((u64)sgi << 24) | ((m >> 16 & 0xff) << 32) | ((m >> 32 & 0xff) << 48);
    __asm__ volatile ("dsb sy; msr " ICC_SGI1R_EL1 ", %0; isb" :: "r" (v));
  } else {
    W32 (gicd_va + D_SGIR, ((1u << cpu) << 16) | sgi);
    dsb_isb ();
  }
}
