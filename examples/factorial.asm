; Compute 5! = 120 in R0 and print it.
        LOADI R0, 1
        LOADI R1, 5
        LOADI R2, 1
loop:
        MUL   R0, R1
        SUB   R1, R2
        JNZ   loop
        OUT   R0
        HALT
