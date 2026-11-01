// Ulix ARM64 -- shared definitions
#ifndef ULIX_H
#define ULIX_H

typedef unsigned long u64;
typedef unsigned int  u32;
typedef unsigned char u8;

#define KERNEL_VA   0xFFFF000000000000UL     // linear map: virtual = KERNEL_VA + physical
#define KVA(pa)     ((void *)(KERNEL_VA + (u64)(pa)))
#define PA(va)      ((u64)(va) - KERNEL_VA)
#define PAGE_SIZE   4096UL

// user address space layout (TTBR0)
#define USER_CODE       0x00400000UL
#define USER_STACK_TOP  0x80000000UL
#define USER_MIN        0x00400000UL
#define USER_MAX        0x80000000UL

// page table entry bits (4 KB granule)
#define PTE_VALID   0x1UL
#define PTE_TABLE   0x3UL
#define PTE_PAGE    0x3UL                   // level 3: bits[1:0] = 11
#define PTE_ATTR(n) ((u64)(n) << 2)         // MAIR index: 0 = device, 1 = normal
#define PTE_AP_USER (1UL << 6)              // AP[1]: accessible from EL0
#define PTE_RO      (1UL << 7)              // AP[2]: read-only
#define PTE_SH      (3UL << 8)
#define PTE_AF      (1UL << 10)
#define PTE_PXN     (1UL << 53)
#define PTE_UXN     (1UL << 54)
#define PTE_ADDR    0x0000FFFFFFFFF000UL

#define PAGE_RW     (PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_PXN | PTE_UXN)
#define PAGE_RO     (PAGE_RW | PTE_RO)
#define PAGE_URX    (PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_AP_USER | PTE_RO | PTE_PXN)
#define PAGE_URW    (PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_AP_USER | PTE_PXN | PTE_UXN)

// register frame pushed by the exception vectors (see vectors.S)
struct frame { u64 x[31]; u64 elr; u64 spsr; };

// console
void putch (char c);
void printf (const char *fmt, ...);

// irq.c
void irq_init (void);
extern volatile u64 ticks;
extern volatile int expect_fault, faults, procs_killed;
extern volatile u64 fault_esr, fault_far;
extern int sys_write_calls;

// mmu.c
void frames_init (void);
u64  alloc_frame (void);                    // zeroed frame, returns physical address
u64  alloc_frames_contig (int n);           // n zeroed, physically contiguous frames
void free_frame (u64 pa);
extern long frames_in_use;
int  map_page (u64 va, u64 pa, u64 flags);                   // kernel (TTBR1)
int  map_page_in (u64 l0_pa, u64 va, u64 pa, u64 flags);     // any address space
u64  unmap_page (u64 va);
u64  virt_to_phys (u64 va);
void user_mm_init (void);
extern u64 empty_l0_pa;
u64  new_user_l0 (void);
void free_user_space (u64 l0_pa);
void sync_icache (void *p, u64 len);

// sched.c
void sched_init (void);
int  task_create_user (const void *code_start, const void *code_end, u64 arg);
void schedule (void);
void schedule_preempt (void);
void task_exit (void) __attribute__ ((noreturn));
int  task_pid (void);
int  tasks_live (void);
void tasks_reap (void);
extern int context_switches, tasks_reaped, preemptions;

// user.S
extern char user_hello_start[], user_hello_end[], user_bad_start[], user_bad_end[];

#endif
