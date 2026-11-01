// start of every user program.  The kernel (u_execv / arch_start_user) leaves  [argc][argv]
// at the top of the stack; main (argc, argv) is called as an ordinary function.
        .section .text.start
        .global _start
_start:
        ldp     x0, x1, [sp]            // x0 = argc, x1 = argv
        bl      main                    // returns the exit status in x0
        mov     x1, x0                  // exit (status): system call 1, argument in x1
        mov     x0, #1
        svc     #0
1:      b       1b
