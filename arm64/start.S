// Ulix ARM64 -- boot code for qemu-system-aarch64 -M virt
// qemu enters at the *physical* address 0x40080000 with the MMU off, at EL1 (or EL2).
// The kernel is linked at 0xFFFF000000000000 + PA, so everything up to the jump to
// the high address must be position independent (adr, no absolute symbol loads).

#define KERNEL_VA   0xFFFF000000000000

// descriptor bits
#define D_BLOCK     0x1
#define D_TABLE     0x3
#define D_ATTR(n)   ((n) << 2)       // MAIR index
#define D_SH_INNER  (3 << 8)
#define D_AF        (1 << 10)
#define D_PXN       (1 << 53)
#define D_UXN       (1 << 54)

        .section .text.boot
        .global _start
_start:
        mrs     x0, mpidr_el1
        and     x0, x0, #0xff
        cbnz    x0, park

        // drop from EL2 to EL1 if we were started there
        mrs     x0, CurrentEL
        lsr     x0, x0, #2
        cmp     x0, #2
        b.ne    at_el1
        mov     x0, #(1 << 31)          // HCR_EL2.RW: EL1 is AArch64
        msr     hcr_el2, x0
        mov     x0, #0x3c5              // SPSR: EL1h, DAIF masked
        msr     spsr_el2, x0
        adr     x0, at_el1
        msr     elr_el2, x0
        eret

at_el1:
        mov     x0, #(3 << 20)          // enable FP/SIMD
        msr     cpacr_el1, x0
        isb

        // ---- zero the four page-table pages (we are running at the physical address)
        adr     x1, pg_t0_l0
        mov     x2, #(4 * 4096 / 8)
1:      str     xzr, [x1], #8
        subs    x2, x2, #1
        b.ne    1b

        // ---- TTBR1 (kernel, upper half): L0[0] -> L1; L1[0] = device 1GB, L1[1] = RAM 1GB
        adr     x1, pg_t1_l0
        adr     x2, pg_t1_l1
        orr     x3, x2, #D_TABLE
        str     x3, [x1]
        ldr     x3, =(D_BLOCK | D_ATTR(0) | D_AF | D_PXN | D_UXN)
        str     x3, [x2]
        ldr     x3, =(0x40000000 | D_BLOCK | D_ATTR(1) | D_SH_INNER | D_AF)
        str     x3, [x2, #8]

        // ---- TTBR0 (identity map of RAM, only needed until we run at the high address)
        adr     x4, pg_t0_l0
        adr     x5, pg_t0_l1
        orr     x3, x5, #D_TABLE
        str     x3, [x4]
        ldr     x3, =(0x40000000 | D_BLOCK | D_ATTR(1) | D_SH_INNER | D_AF)
        str     x3, [x5, #8]

        // ---- MMU configuration
        ldr     x0, =0xff00             // MAIR: attr0 = device-nGnRnE, attr1 = normal write-back
        msr     mair_el1, x0
        ldr     x0, =((16) | (1 << 8) | (1 << 10) | (3 << 12) | (0 << 14) | \
                      (16 << 16) | (1 << 24) | (1 << 26) | (3 << 28) | (2 << 30))
        mrs     x6, id_aa64mmfr0_el1
        and     x6, x6, #7              // PARange -> IPS
        orr     x0, x0, x6, lsl #32
        msr     tcr_el1, x0
        msr     ttbr0_el1, x4
        msr     ttbr1_el1, x1
        isb
        tlbi    vmalle1
        dsb     nsh
        isb
        mrs     x0, sctlr_el1
        orr     x0, x0, #(1 << 0)       // M: MMU on
        orr     x0, x0, #(1 << 2)       // C: data cache
        orr     x0, x0, #(1 << 12)      // I: instruction cache
        msr     sctlr_el1, x0
        isb

        ldr     x0, =high_entry         // absolute (link-time) high address
        br      x0

        .text
high_entry:
        ldr     x0, =stack_top
        mov     sp, x0
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        str     xzr, [x0], #8
        b       1b
2:      bl      kmain
park:   wfe
        b       park

        // 4 page-table pages; kernel_l0 lets C code walk the kernel tables
        .section .pgtbl, "aw", @nobits
        .balign 4096
pg_t0_l0:  .skip 4096
pg_t0_l1:  .skip 4096
        .global kernel_l0
kernel_l0:
pg_t1_l0:  .skip 4096
pg_t1_l1:  .skip 4096

        .section .bss
        .balign 16
        .skip 0x4000
stack_top:
