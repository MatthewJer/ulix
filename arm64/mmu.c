// Ulix ARM64 -- physical frame allocator and 4-level page table code
// (replaces the x86 2-level page directory / page table code and KMAP/KMAPD)

typedef unsigned long u64;
typedef unsigned int  u32;

extern void printf (const char *fmt, ...);
extern u64 kernel_l0[512];
extern char __kernel_end[];

#define KERNEL_VA   0xFFFF000000000000UL
#define KVA(pa)     ((void *)(KERNEL_VA + (u64)(pa)))
#define PA(va)      ((u64)(va) - KERNEL_VA)
#define PAGE_SIZE   4096UL

#define RAM_START   0x40000000UL
#define RAM_SIZE    (128UL * 1024 * 1024)   // TODO: read from the device tree (like MEM_SIZE on x86)

// ---- frame allocator: bump pointer first, free list for released frames ----
static u64 next_free;                       // physical address
static u64 free_list = 0;                   // singly linked through the frames themselves

void frames_init (void) {
  next_free = (PA (__kernel_end) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  printf ("RAM: %lu MByte at 0x%lx, kernel ends at 0x%lx, first free frame 0x%lx\n",
          RAM_SIZE >> 20, RAM_START, PA (__kernel_end), next_free);
}

static void zero_page (void *p) {
  u64 *q = p;
  for (int i = 0; i < PAGE_SIZE / 8; i++) q[i] = 0;
}

u64 alloc_frame (void) {                    // returns a zeroed frame (physical address)
  u64 pa;
  if (free_list) {
    pa = free_list;
    free_list = *(u64 *)KVA (pa);
  } else {
    if (next_free + PAGE_SIZE > RAM_START + RAM_SIZE) { printf ("out of memory\n"); for (;;) ; }
    pa = next_free;
    next_free += PAGE_SIZE;
  }
  zero_page (KVA (pa));
  return pa;
}

void free_frame (u64 pa) {
  *(u64 *)KVA (pa) = free_list;
  free_list = pa;
}

// ---- page tables ----
#define PTE_VALID   0x1UL
#define PTE_TABLE   0x3UL
#define PTE_PAGE    0x3UL                   // level 3: bits[1:0] = 11
#define PTE_ATTR(n) ((u64)(n) << 2)
#define PTE_RO      (1UL << 7)              // AP[2]: read-only at EL1
#define PTE_SH      (3UL << 8)
#define PTE_AF      (1UL << 10)
#define PTE_PXN     (1UL << 53)
#define PTE_UXN     (1UL << 54)
#define PTE_ADDR    0x0000FFFFFFFFF000UL

#define PAGE_RW     (PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_PXN | PTE_UXN)
#define PAGE_RO     (PAGE_RW | PTE_RO)

#define IDX(va, lvl) (((va) >> (39 - 9 * (lvl))) & 511)

// returns pointer to the level-3 entry for va; creates missing tables if 'create'
static u64 *walk (u64 va, int create) {
  u64 *table = kernel_l0;
  for (int lvl = 0; lvl < 3; lvl++) {
    u64 *e = &table[IDX (va, lvl)];
    if (!(*e & PTE_VALID)) {
      if (!create) return 0;
      *e = alloc_frame () | PTE_TABLE;
    } else if ((*e & 3) == 1) {
      return 0;                             // 1 GB / 2 MB block: can't descend
    }
    table = KVA (*e & PTE_ADDR);
  }
  return &table[IDX (va, 3)];
}

int map_page (u64 va, u64 pa, u64 flags) {
  u64 *e = walk (va, 1);
  if (!e || (*e & PTE_VALID)) return -1;
  *e = (pa & PTE_ADDR) | flags;
  __asm__ volatile ("dsb ishst; isb");
  return 0;
}

u64 unmap_page (u64 va) {                   // returns the physical address that was mapped
  u64 *e = walk (va, 0);
  if (!e || !(*e & PTE_VALID)) return 0;
  u64 pa = *e & PTE_ADDR;
  *e = 0;
  __asm__ volatile ("dsb ishst; tlbi vaae1is, %0; dsb ish; isb" :: "r" (va >> 12));
  return pa;
}

// translate with the hardware (AT instruction), like mmu_p() on x86; 0 = not mapped
u64 virt_to_phys (u64 va) {
  u64 par;
  __asm__ volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (va));
  if (par & 1) return 0;
  return (par & PTE_ADDR) | (va & 0xfff);
}

// the identity map is only needed during boot; later TTBR0 will hold user address spaces
void drop_identity_map (void) {
  u64 tcr;
  __asm__ volatile ("mrs %0, tcr_el1" : "=r" (tcr));
  tcr |= (1UL << 7);                        // EPD0: no walks through TTBR0
  __asm__ volatile ("msr tcr_el1, %0; tlbi vmalle1; dsb nsh; isb" :: "r" (tcr));
}
