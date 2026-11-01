// Ulix ARM64 -- user programs (run at EL0, position independent)
// Syscall ABI: svc #0; x8 = number, x0..x2 = arguments, result in x0
//   1 write (fd, buf, len)   2 getpid   3 yield   4 exit

        .section .user_prog, "a"

// Prints "Hello from process P round R" five times, then exits.  x0 = P (single digit).
        .global user_hello_start, user_hello_end
user_hello_start:
        mov     x19, x0
        mov     x20, #0
round:
        sub     sp, sp, #32
        adr     x1, msg
        mov     x2, sp
        mov     x3, #0
copy:   ldrb    w4, [x1, x3]
        strb    w4, [x2, x3]
        add     x3, x3, #1
        cmp     x3, #29
        b.ne    copy
        add     w4, w19, #'0'
        strb    w4, [sp, #19]
        add     w4, w20, #'0'
        strb    w4, [sp, #27]
        mov     x0, #1
        mov     x1, sp
        mov     x2, #29
        mov     x8, #1
        svc     #0
        add     sp, sp, #32
        movz    x5, #0x2d00             // busy loop of 20,000,000 iterations: lets the
        movk    x5, #0x131, lsl #16     // timer interrupt preempt us
delay:  subs    x5, x5, #1
        b.ne    delay
        add     x20, x20, #1
        cmp     x20, #5
        b.ne    round
        mov     x0, #0
        mov     x8, #4
        svc     #0
hang:   b       hang
msg:    .ascii  "Hello from process ? round ?\n"
user_hello_end:

// Misbehaving program: reads kernel memory. The kernel must kill it, not crash.
        .global user_bad_start, user_bad_end
user_bad_start:
        movz    x1, #0xffff, lsl #48
        movk    x1, #0x4008, lsl #16
        ldr     x0, [x1]
        mov     x0, #0                  // never reached
        mov     x8, #4
        svc     #0
user_bad_end:
