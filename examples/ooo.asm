; Out-of-order demo: each iteration has an independent long-latency DIV feeding a
; dependent ADD, plus independent accumulation behind it. In-order issue stalls
; the independent work behind the DIV's consumer; OoO executes it concurrently.
        LOADI R0, 0          ; independent accumulator
        LOADI R1, 1
        LOADI R3, 2          ; divisor
        LOADI R4, 20         ; iterations
        LOADI R5, 0          ; sink for the DIV chain
loop:
        LOADI R2, 1000       ; fresh dividend (no loop-carried dep on R2)
        DIV   R2, R3         ; ~20-cycle latency
        ADD   R5, R2         ; consumes the DIV result
        ADD   R0, R1         ; independent work
        ADD   R0, R1
        ADD   R0, R1
        SUB   R4, R1
        JNZ   loop
        OUT   R0             ; 20 * 3 = 60
        HALT
