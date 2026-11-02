// Ulix ARM64 -- physical frame allocator and 4-level page table code
// (replaces the x86 2-level page directory / page table code and KMAP/KMAPD)

#include "ulix.h"

extern u64 kernel_l0[512];
extern int arch_cow_fault (u64 va);
extern int arch_user_fault (u64 va);
extern char __kernel_end[];

#define RAM_START   0x40000000UL
#define RAM_SIZE    (128UL * 1024 * 1024)   // TODO: read from the device tree (like MEM_SIZE on x86)

// ---- frame allocator: bump pointer first, free list for released frames ----
static u64 next_free;                       // physical address
static unsigned short frame_refs[RAM_SIZE / PAGE_SIZE];     // how many page table entries use a frame (COW)
#define REFS(pa)  frame_refs[((pa) - RAM_START) / PAGE_SIZE]
static u64 free_list = 0;                   // singly linked through the frames themselves
long frames_in_use = 0;

void frames_init (void) {
  next_free = (PA (__kernel_end) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  printf ("RAM: %lu MByte at 0x%lx, kernel ends at 0x%lx, first free frame 0x%lx\n",
          RAM_SIZE >> 20, RAM_START, PA (__kernel_end), next_free);
}

static void zero_page (void *p) {
  u64 *q = p;
  for (int i = 0; i < PAGE_SIZE / 8; i++) q[i] = 0;
}

static u64 bump (void) {
  if (next_free + PAGE_SIZE > RAM_START + RAM_SIZE) { printf ("out of memory\n"); for (;;) ; }
  u64 pa = next_free;
  next_free += PAGE_SIZE;
  return pa;
}

u64 alloc_frame (void) {                    // returns a zeroed frame (physical address)
  u64 pa;
  if (free_list) {
    pa = free_list;
    free_list = *(u64 *)KVA (pa);
  } else {
    pa = bump ();
  }
  zero_page (KVA (pa));
  frames_in_use++;
  REFS (pa) = 1;
  return pa;
}

u64 alloc_frames_contig (int n) {           // only the bump area is contiguous
  u64 first = bump ();
  zero_page (KVA (first));
  REFS (first) = 1;
  for (int i = 1; i < n; i++) { u64 pa = bump (); zero_page (KVA (pa)); REFS (pa) = 1; }
  frames_in_use += n;
  return first;
}

// give up one reference; the frame is free when nobody uses it any more
void free_frame (u64 pa) {
  if (--REFS (pa) > 0) return;
  *(u64 *)KVA (pa) = free_list;
  free_list = pa;
  frames_in_use--;
}

void get_frame (u64 pa)       { REFS (pa)++; }                  // one more user of the frame (fork shares pages)
int  frame_refcount (u64 pa)  { return REFS (pa); }

// ---- page tables ----
#define IDX(va, lvl) (((va) >> (39 - 9 * (lvl))) & 511)

// returns pointer to the level-3 entry for va; creates missing tables if 'create'
static u64 *walk (u64 *l0, u64 va, int create) {
  u64 *table = l0;
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

// the level-3 entry of a user address (valid or not), 0 if the tables do not exist
u64 *pte_of (u64 l0_pa, u64 va) { return walk (KVA (l0_pa), va & ~(PAGE_SIZE - 1), 0); }

int map_page_in (u64 l0_pa, u64 va, u64 pa, u64 flags) {
  u64 *e = walk (KVA (l0_pa), va, 1);
  if (!e || (*e & PTE_VALID)) return -1;
  *e = (pa & PTE_ADDR) | flags;
  __asm__ volatile ("dsb ishst; isb");
  return 0;
}

int map_page (u64 va, u64 pa, u64 flags) {
  return map_page_in (PA (kernel_l0), va, pa, flags);
}

u64 unmap_page (u64 va) {                   // returns the physical address that was mapped
  u64 *e = walk (kernel_l0, va, 0);
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

// Translate a pointer that came from user space. Only the user range is accepted, and for
// writes the page must really be writable. The kernel then accesses the data through the
// linear map and never dereferences user pointers itself.
u64 user_to_phys (u64 va, int write) {
  if (va < USER_MIN || va >= USER_MAX) return 0;
  u64 par = 1;
  for (int tries = 0; tries < 4 && (par & 1); tries++) {
    if (write) __asm__ volatile ("at s1e1w, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (va));
    else       __asm__ volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (va));
    if (!(par & 1)) break;
    // not translated: a shared page (copy it), a page that was paged out (read it back) or a cleared access flag
    if (!((write && arch_cow_fault (va)) || arch_user_fault (va))) break;
  }
  if (par & 1) return 0;
  return (par & PTE_ADDR) | (va & 0xfff);
}

// ---- user address spaces (TTBR0) ----
u64 empty_l0_pa;
int asid_16bit = 0;                         // 1: the CPU has 16-bit ASIDs, address space switches need no TLB flush

// The boot identity map is only needed until we run at the high address. From now on
// TTBR0 belongs to user processes; the kernel itself lives in TTBR1.
void user_mm_init (void) {
  empty_l0_pa = alloc_frame ();
  u64 tcr;
  __asm__ volatile ("mrs %0, tcr_el1" : "=r" (tcr));
  tcr &= ~(1UL << 7);                       // EPD0 = 0: walk TTBR0 again
  u64 mmfr0;
  __asm__ volatile ("mrs %0, id_aa64mmfr0_el1" : "=r" (mmfr0));
  if (((mmfr0 >> 4) & 15) == 2) { tcr |= 1UL << 36; asid_16bit = 1; }   // TCR.AS: 16-bit ASIDs (one per address space)
  __asm__ volatile ("msr ttbr0_el1, %0; msr tcr_el1, %1; tlbi vmalle1; dsb nsh; isb"
                    :: "r" (empty_l0_pa), "r" (tcr));
}

u64 new_user_l0 (void) { return alloc_frame (); }

static void free_table (u64 pa, int lvl) {
  u64 *t = KVA (pa);
  for (int i = 0; i < 512; i++) {
    u64 e = t[i];
    if (!(e & PTE_VALID)) continue;
    if (lvl < 3) free_table (e & PTE_ADDR, lvl + 1);
    else         free_frame (e & PTE_ADDR);
  }
  free_frame (pa);
}

void free_user_space (u64 l0_pa) { free_table (l0_pa, 0); }

// make freshly written code visible to instruction fetch
void sync_icache (void *p, u64 len) {
  u64 a = (u64)p & ~63UL;
  for (; a < (u64)p + len; a += 64) __asm__ volatile ("dc cvau, %0" :: "r" (a) : "memory");
  __asm__ volatile ("dsb ish; ic ialluis; dsb ish; isb");
}


// ---- copy-on-write ----
// fork() maps the pages of the parent in the child, too, and marks both entries read-only with PTE_COW.
// The first write (by the process, or by the kernel inside a system call) causes a permission fault;
// cow_fault() then gives the writer its own copy. Returns 1 if the fault was a COW fault and is resolved.
int cow_fault (u64 l0_pa, u64 va) {
  if (va < USER_MIN || va >= USER_MAX) return 0;
  u64 *e = walk (KVA (l0_pa), va & ~(PAGE_SIZE - 1), 0);
  if (!e || !(*e & PTE_VALID) || !(*e & PTE_COW)) return 0;
  u64 old = *e & PTE_ADDR, flags = *e & ~(PTE_ADDR | PTE_RO | PTE_COW);
  if (REFS (old) == 1) {                                   // the last user: just make it writable again
    *e = old | flags;
  } else {
    u64 pa = alloc_frame ();
    u64 *src = KVA (old), *dst = KVA (pa);
    for (int i = 0; i < (int)(PAGE_SIZE / 8); i++) dst[i] = src[i];
    REFS (old)--;                                          // we no longer use the shared frame
    *e = pa | flags;
  }
  __asm__ volatile ("dsb ishst; tlbi vaae1is, %0; dsb ish; isb" :: "r" (va >> 12));
  return 1;
}
