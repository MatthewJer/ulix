// Ulix ARM64 -- page table entry bits (4 KByte granule), shared by all ARM64 sources
#ifndef ARCH_PTE_H
#define ARCH_PTE_H
#ifndef ULIX_H
typedef unsigned long u64;
#endif

// page table entry bits (4 KB granule)
#define PTE_VALID   0x1UL
#define PTE_TABLE   0x3UL
#define PTE_PAGE    0x3UL                   // level 3: bits[1:0] = 11
#define PTE_ATTR(n) ((u64)(n) << 2)         // MAIR index: 0 = device, 1 = normal
#define PTE_AP_USER (1UL << 6)              // AP[1]: accessible from EL0
#define PTE_RO      (1UL << 7)              // AP[2]: read-only
#define PTE_SH      (3UL << 8)
#define PTE_AF      (1UL << 10)
#define PTE_NG      (1UL << 11)             // not global: the TLB entry is tagged with the ASID (user pages only)
#define PTE_PXN     (1UL << 53)
#define PTE_UXN     (1UL << 54)
#define PTE_COW     (1UL << 55)             // software bit (ignored by the hardware): copy-on-write page
#define PTE_ADDR    0x0000FFFFFFFFF000UL

#define PAGE_RW     (PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_PXN | PTE_UXN)
#define PAGE_RO     (PAGE_RW | PTE_RO)
#define PAGE_URX    (PTE_NG | PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_AP_USER | PTE_RO | PTE_PXN)
#define PAGE_URO    (PTE_NG | PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_AP_USER | PTE_RO | PTE_PXN | PTE_UXN)
#define PAGE_URW    (PTE_NG | PTE_PAGE | PTE_ATTR (1) | PTE_SH | PTE_AF | PTE_AP_USER | PTE_PXN | PTE_UXN)


#endif
