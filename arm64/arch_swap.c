// Ulix ARM64 -- swapping (ulix-book.nw: "The Swap File", "Page Replacement Strategy")
//
// The book's swap file code (write_swap_page, read_swap_page, the paging[] table, the hash function and the
// counter table) is used unchanged.  What depends on the x86 page tables is written here for the 4-level
// AArch64 tables, with the same names and the same behavior:
//   page_out / page_in        the "present" bit of the x86 descriptor is the valid bit of the AArch64 entry;
//                             the "paged_out" bit is the software bit PTE_SWAPPED (the other bits stay in the
//                             entry, so that page_in restores the permissions, copy-on-write marks, ...)
//   the accessed bit          AArch64 CPUs without hardware access flag management (like the one that qemu
//                             emulates) fault on the first access to a page whose AF bit is clear.  The counter
//                             update clears AF where it counts an access, and arch_user_fault sets it again.
//   page replacement          update/rescale counters (timer tasks of the book) and "free one frame"
//   syscalls                  page_out (508), get_free_frames (509), free_a_frame (510)
// Pages whose frame is shared (copy-on-write after fork) are never paged out.

// the book defines hash() in the header (every file that includes it gets a copy): keep this copy private
#define hash arch_swap_hash
#include "ulix-api.h"
#undef hash
#define hash arch_swap_hash
#include "arch_pte.h"

#define KVA(pa)  ((void *)(0xFFFF000000000000UL + (u64)(pa)))
#define USER_LO  0x00400000UL
#define USER_HI  0x80000000UL
#define PAGE     4096UL

extern u64  arch_as_l0 (addr_space_id id);
extern u64 *pte_of (u64 l0_pa, u64 va);
extern u64  alloc_frame (void);
extern void free_frame (u64 pa);
extern int  frame_refcount (u64 pa);
extern int  asid_16bit;
extern long frames_in_use;
extern u64  arch_ram_frames (void);

unsigned int mmu_p (addr_space_id id, unsigned int pageno) { return (unsigned int)-1; }        // the book's swap functions call it when no frame number is given

static void flush_page (addr_space_id as, u64 va) {
  if (asid_16bit) __asm__ volatile ("dsb ishst; tlbi vae1is, %0; dsb ish; isb" :: "r" (((u64)as << 48) | (va >> 12)) : "memory");
  else            __asm__ volatile ("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

static void flush_as (addr_space_id as) {
  if (asid_16bit) __asm__ volatile ("dsb ishst; tlbi aside1is, %0; dsb ish; isb" :: "r" ((u64)as << 48) : "memory");
  else            __asm__ volatile ("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

// ---- swap file ----
void arch_swap_init (void) {
  paging_lock  = get_new_lock ("paging");        // the book creates these in "initialize kernel global variables"
  swapper_lock = get_new_lock ("swapper");
  swap_fd = u_open ("/tmp/swap", O_RDWR, 0);
  if (swap_fd == -1) swap_fd = u_open ("/tmp/swap", O_RDWR | O_CREAT, 0);   // the file grows with the pages written to it
  if (swap_fd != -1)
    printf ("swapon: enabling /tmp/swap (up to %d KByte)\n", MAX_SWAP_FRAMES * 4);
  else
    printf ("swapon: error opening /tmp/swap!\n");
}

// give the swap file entries of an address space back (the book never frees them, but its address space ids
// are not reused for another program: here an id is reused by exec and by new processes)
void arch_swap_release_as (addr_space_id as) {
  for (int i = 0; i < MAX_SWAP_FRAMES; i++)
    if (paging[i].used && paging[i].as == as) paging[i].used = false;
}

static int swap_index (int as, int pageno) {
  for (int i = 0; i < MAX_SWAP_FRAMES; i++)
    if (paging[i].used && paging[i].as == as && paging[i].pageno == pageno) return i;
  return -1;
}

// ---- page_out and page_in ----
// The swap file has one file position for everybody, and a process that waits for the disk lets others run: one
// transfer at a time.
static lock swap_io_lock = NULL;
static int swap_io_enter (void) {
  if (current_task <= 1) return 0;
  if (!swap_io_lock) swap_io_lock = get_new_lock ("swap io");
  mutex_lock (swap_io_lock);
  return 1;
}
static void swap_io_leave (int locked) { if (locked) mutex_unlock (swap_io_lock); }

int page_out (int as, int pageno) {
  u64 l0 = arch_as_l0 (as), va = (u64)pageno << 12;
  if (!l0 || swap_fd == -1 || va < USER_LO || va >= USER_HI) return -1;
  u64 *e = pte_of (l0, va);
  if (!e || !(*e & PTE_VALID)) return -1;                  // fail: page not found
  u64 pa = *e & PTE_ADDR;
  if (frame_refcount (pa) != 1) return -1;                 // shared with another process (copy-on-write)
  int l = swap_io_enter ();
  e = pte_of (l0, va);                                     // (the tables may have changed while we waited)
  if (!e || !(*e & PTE_VALID) || (*e & PTE_ADDR) != pa) { swap_io_leave (l); return -1; }
  if (write_swap_page (as, pageno, (int)(pa >> 12)) != 0) { swap_io_leave (l); return -1; }
  int index = swap_index (as, pageno);
  if (index < 0 || u_lseek (swap_fd, 0, SEEK_END) < (index + 1) * (int)PAGE) {   // the disk is full
    if (index >= 0) paging[index].used = false;
    swap_io_leave (l);
    return -1;
  }
  swap_io_leave (l);
  *e = (*e & ~PTE_VALID) | PTE_SWAPPED;                    // mark page non-present and paged-out
  flush_page (as, va);
  free_frame (pa);                                         // the frame is free again
  return 0;
}

int page_in (int as, int pageno) {
  u64 l0 = arch_as_l0 (as), va = (u64)pageno << 12;
  if (!l0 || swap_fd == -1) return -1;
  u64 *e = pte_of (l0, va);
  if (!e || (*e & PTE_VALID) || !(*e & PTE_SWAPPED)) return -1;      // fail: page was not paged out
  if (swap_index (as, pageno) < 0) return -1;
  u64 pa = alloc_frame ();                                 // reserve a frame
  int l = swap_io_enter ();
  e = pte_of (l0, va);
  if (!e || (*e & PTE_VALID)) {                            // somebody else brought the page back while we waited
    swap_io_leave (l);  free_frame (pa);
    return (e && (*e & PTE_VALID)) ? 0 : -1;
  }
  read_swap_page (as, pageno, (int)(pa >> 12));
  swap_io_leave (l);
  e = pte_of (l0, va);
  if (!e || (*e & PTE_VALID)) { free_frame (pa); return (e && (*e & PTE_VALID)) ? 0 : -1; }
  *e = pa | ((*e & ~(PTE_ADDR | PTE_SWAPPED)) | PTE_VALID | PTE_AF);
  __asm__ volatile ("dsb ishst; isb" ::: "memory");
  return 0;
}

// Fault handler part of the book: "check if page was paged out".  Also resolves the access flag faults.
// Returns 1 if the instruction can be repeated.
// The user mode stack grows by a page when the program touches the page just below it ("page fault handler:
// enlarge user mode stack" of the book; the stack is limited to 64 MByte and must not run into the heap).
#define USER_STACK_TOP   0x80000000UL
#define MAX_STACK        (64UL << 20)
extern int map_page_in (u64 l0_pa, u64 va, u64 pa, u64 flags);

static int grow_stack (u64 l0, u64 va) {
  address_space *as = &address_spaces[current_as];
  u64 bottom = USER_STACK_TOP - as->stacksize;
  if (va > USER_STACK_TOP || va + PAGE < bottom) return 0;                  // not just below the stack
  if (as->stacksize >= MAX_STACK || bottom - PAGE < as->memend + PAGE) return 0;
  if (map_page_in (l0, bottom - PAGE, alloc_frame (), PAGE_URW) != 0) return 0;
  as->stacksize += PAGE;
  return 1;
}

int arch_user_fault (u64 va) {
  if (va < USER_LO || va >= USER_HI) return 0;
  u64 l0 = arch_as_l0 (current_as);
  if (!l0) return 0;
  u64 *e = pte_of (l0, va);
  if (!e || (!(*e & PTE_VALID) && !(*e & PTE_SWAPPED))) return grow_stack (l0, va);
  if (!(*e & PTE_VALID)) return page_in (current_as, va >> 12) == 0;
  if (!(*e & PTE_AF)) *e |= PTE_AF;
  flush_page (current_as, va);
  return 1;     // (if the entry is fine already, another thread of the process on another CPU was faster: just repeat the access)
}

// release_page of the book: take a page out of the current address space and free its frame (used by the
// kernel shell command rp through release_page_range, which is the book's)
void release_page (unsigned int pageno) {
  u64 l0 = arch_as_l0 (current_as), va = (u64)pageno << 12;
  if (!l0 || va < USER_LO || va >= USER_HI) return;
  u64 *e = pte_of (l0, va);
  if (!e || !(*e & (PTE_VALID | PTE_SWAPPED))) return;                     // no such page
  u64 pa = *e & PTE_ADDR;
  int present = (*e & PTE_VALID) != 0;
  *e = 0;
  flush_page (current_as, va);
  if (present) free_frame (pa);
}

// ---- page replacement: counters ----
// visit every valid user page of an address space; with 'count' the access flag is evaluated and cleared,
// otherwise the page with the lowest counter is searched
static void walk_pages (u64 table_pa, int lvl, u64 prefix, addr_space_id as, int count, int *pick_pageno, int *themin) {
  u64 *t = KVA (table_pa);
  for (int i = 0; i < 512; i++) {
    u64 va = prefix | ((u64)i << (39 - 9 * lvl));
    if (va >= USER_HI) return;
    u64 e = t[i];
    if (!(e & PTE_VALID)) continue;
    if (lvl < 3) { walk_pages (e & PTE_ADDR, lvl + 1, va, as, count, pick_pageno, themin); continue; }
    int pageno = (int)(va >> 12);
    int index = hash ((as << 20) | pageno, PG_MAX_COUNTERS);
    if (count) {
      if (e & PTE_AF) {                                    // accessed since the last time
        t[i] = e & ~PTE_AF;
        if (index < PG_MAX_COUNTERS) { counter_table[index].used = true; counter_table[index].count++; }
      }
    } else if (va >= USER_LO && frame_refcount (e & PTE_ADDR) == 1) {
      int c = counter_table[index].used ? counter_table[index].count : 0;
      if (*pick_pageno < 0 || c < *themin) { *pick_pageno = pageno; *themin = c; }
    }
  }
}

void arch_update_counters (void) {
  if (!mutex_try_lock (paging_lock)) return;
  for (int as = 2; as < MAX_ADDR_SPACES; as++) {
    if (address_spaces[as].status == AS_FREE || !arch_as_l0 (as)) continue;
    int dummy_page = -1, dummy_min = 0;
    walk_pages (arch_as_l0 (as), 0, 0, as, 1, &dummy_page, &dummy_min);
    flush_as (as);                                         // the cleared access flags must be noticed
  }
  mutex_unlock (paging_lock);
}

void arch_rescale_counters (void) {
  int themax = 0;
  if (!mutex_try_lock (paging_lock)) return;
  for (int index = 0; index < PG_MAX_COUNTERS; index++)
    if (counter_table[index].used && counter_table[index].count > themax) themax = counter_table[index].count;
  if (themax > PG_COUNTER_THRESHOLD) {
    for (int index = 0; index < PG_MAX_COUNTERS; index++)
      if (counter_table[index].used) { counter_table[index].count /= 2; counter_table[index].count += 1; }
  }
  mutex_unlock (paging_lock);
}

// ---- system calls ----
void syscall_page_out (context_t *r) {
  // ebx: page number
  eax_return (page_out (current_as, r->ebx));
}

// "page replacement: free one frame": page out the page with the lowest counter
void syscall_free_a_frame (context_t *r) {
  addr_space_id pick_as = -1;
  int pick_pageno = -1, themin = 0;
  while (!mutex_try_lock (paging_lock)) ;
  for (int as = 2; as < MAX_ADDR_SPACES; as++) {
    if (address_spaces[as].status != AS_USED || !arch_as_l0 (as)) continue;
    int p = -1, m = 0;
    walk_pages (arch_as_l0 (as), 0, 0, as, 0, &p, &m);
    if (p >= 0 && (pick_as == -1 || m < themin)) { pick_as = as; pick_pageno = p; themin = m; }
  }
  mutex_unlock (paging_lock);
  if (pick_as != -1) {
    mutex_lock (paging_lock);
    page_out (pick_as, pick_pageno);
    mutex_unlock (paging_lock);
  } else {
    printf ("\nERROR: cannot pick a page to evict!\n");
  }
}

// the swapper blocks on swapper_lock, which the timer releases every 0.1 seconds
void syscall_get_free_frames (context_t *r) {
  mutex_lock (swapper_lock);
  eax_return ((long)arch_ram_frames () - frames_in_use);
}

// timer tasks of the book that concern paging; called from the timer interrupt
void arch_paging_timer_tasks (void) {
  if (!scheduler_is_active) return;
  if ((system_ticks % 10) == 0) {
    arch_update_counters ();
    if (swapper_lock->bq.next) mutex_unlock (swapper_lock);
  }
  if ((system_ticks % 50) == 5) arch_rescale_counters ();
}
