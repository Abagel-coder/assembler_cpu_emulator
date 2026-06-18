; Advance 10 Fibonacci steps; leaves fib(10) = 55 in R0 and prints it.
        LOADI R0, 0
        LOADI R1, 1
        LOADI R2, 10
        LOADI R3, 1
loop:
        MOV   R4, R0
        ADD   R4, R1
        MOV   R0, R1
        MOV   R1, R4
        SUB   R2, R3
        JNZ   loop
        OUT   R0
        HALT
