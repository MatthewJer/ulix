// Ulix ARM64 -- exception vector table (replaces the x86 IDT)
// Every entry saves the full register frame and calls exc_handler (frame, kind).
// kind: 0 = synchronous, 1 = IRQ, 2 = FIQ, 3 = SError.

        .macro  vector kind
        .balign 128
        sub     sp, sp, #272
        stp     x0,  x1,  [sp, #0]
        mov     x0, #\kind
        b       save_and_call
        .endm

        .section .text
        .balign 2048
        .global vectors
vectors:
        // current EL, SP_EL0
        vector 0
        vector 1
        vector 2
        vector 3
        // current EL, SP_ELx  (the kernel runs here)
        vector 0
        vector 1
        vector 2
        vector 3
        // lower EL, AArch64
        vector 0
        vector 1
        vector 2
        vector 3
        // lower EL, AArch32 (unused)
        vector 0
        vector 1
        vector 2
        vector 3

save_and_call:
        // x0 = kind, original x0/x1 already stored at [sp,#0]
        stp     x2,  x3,  [sp, #16]
        stp     x4,  x5,  [sp, #32]
        stp     x6,  x7,  [sp, #48]
        stp     x8,  x9,  [sp, #64]
        stp     x10, x11, [sp, #80]
        stp     x12, x13, [sp, #96]
        stp     x14, x15, [sp, #112]
        stp     x16, x17, [sp, #128]
        stp     x18, x19, [sp, #144]
        stp     x20, x21, [sp, #160]
        stp     x22, x23, [sp, #176]
        stp     x24, x25, [sp, #192]
        stp     x26, x27, [sp, #208]
        stp     x28, x29, [sp, #224]
        mrs     x1, elr_el1
        mrs     x2, spsr_el1
        stp     x30, x1,  [sp, #240]
        str     x2,       [sp, #256]
        mov     x1, x0              // kind
        mov     x0, sp              // frame
        bl      exc_handler
        ldr     x2,       [sp, #256]
        ldp     x30, x1,  [sp, #240]
        msr     elr_el1, x1
        msr     spsr_el1, x2
        ldp     x28, x29, [sp, #224]
        ldp     x26, x27, [sp, #208]
        ldp     x24, x25, [sp, #192]
        ldp     x22, x23, [sp, #176]
        ldp     x20, x21, [sp, #160]
        ldp     x18, x19, [sp, #144]
        ldp     x16, x17, [sp, #128]
        ldp     x14, x15, [sp, #112]
        ldp     x12, x13, [sp, #96]
        ldp     x10, x11, [sp, #80]
        ldp     x8,  x9,  [sp, #64]
        ldp     x6,  x7,  [sp, #48]
        ldp     x4,  x5,  [sp, #32]
        ldp     x2,  x3,  [sp, #16]
        ldp     x0,  x1,  [sp, #0]
        add     sp, sp, #272
        eret
