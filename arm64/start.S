// Ulix ARM64 -- boot code for qemu-system-aarch64 -M virt
// qemu loads the image at 0x40080000 and enters at EL1 (or EL2 with virtualization=on).

        .section .text.boot
        .global _start
_start:
        // only core 0 continues; the others park
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
        // enable FP/SIMD (compiler may use it for memcpy etc.)
        mov     x0, #(3 << 20)
        msr     cpacr_el1, x0
        isb

        ldr     x0, =stack_top
        mov     sp, x0

        // clear .bss
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        str     xzr, [x0], #8
        b       1b
2:
        bl      kmain
park:   wfe
        b       park

        .section .bss
        .align 4
        .skip 0x4000
stack_top:
