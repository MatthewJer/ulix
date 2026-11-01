// start of every user program: the kernel passes argc in x0 and argv in x1
        .section .text.start
        .global _start
_start:
        bl      main                    // returns the exit status in x0
        mov     x8, #4                  // exit
        svc     #0
1:      b       1b
