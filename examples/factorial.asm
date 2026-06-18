; factorial.asm — compute 5! = 120 into R0, then print it.
        LOADI R0, 1        ; result
        LOADI R1, 5        ; n
        LOADI R2, 1        ; decrement step
loop:
        MUL   R0, R1       ; result *= n
        SUB   R1, R2       ; n--
        JNZ   loop
        OUT   R0           ; prints 120
        HALT
