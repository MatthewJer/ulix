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

#include "arch_pte.h"

// register frame pushed by the exception vectors (see vectors.S)
struct frame { u64 x[31]; u64 elr; u64 spsr; u64 sp_el0; u64 pad[2]; };   // 288 bytes, see context_t in the book

// console
void putch (char c);
int printf (const char *fmt, ...);

// irq.c
void irq_init (void);
extern volatile u64 ticks;
extern volatile int expect_fault, faults, procs_killed;
extern volatile u64 fault_esr, fault_far;

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
u64  user_to_phys (u64 va, int write);      // checked translation of a user pointer, 0 = refused
void user_mm_init (void);
extern u64 empty_l0_pa;
u64  new_user_l0 (void);
void free_user_space (u64 l0_pa);
void sync_icache (void *p, u64 len);

// virtio.c, minixfs.c
int  blk_init (void);
u64  blk_capacity (void);
int  blk_read  (u64 sector, void *buf, u32 nsectors);
int  blk_write (u64 sector, void *buf, u32 nsectors);
int  minix_mount (void);
int  minix_is_file (u32 ino);
u32  minix_lookup (const char *path);
u32  minix_size (u32 ino);
long minix_read (u32 ino, u32 offset, void *buf, u32 len);
int  minix_readdir (u32 dir, void (*fn)(u32 ino, const char *name, void *arg), void *arg);

// elf.c
int  elf_load (u64 l0, u32 ino, u64 *entry);

// kmain.c
void capture_start (void);
int  capture_stop (char **text);            // returns length, text is NUL-terminated

// mmu.c additionally: user pointers are accessed through the checked translation below
// (the processes of the book run on arch_thread.c, see the declarations there)

#endif
