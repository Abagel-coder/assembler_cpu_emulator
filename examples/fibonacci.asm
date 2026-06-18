; fibonacci.asm — advance 10 steps of the Fibonacci sequence.
; Leaves fib(10) = 55 in R0, then prints it.
        LOADI R0, 0        ; a
        LOADI R1, 1        ; b
        LOADI R2, 10       ; counter
        LOADI R3, 1        ; decrement step
loop:
        MOV   R4, R0       ; tmp = a
        ADD   R4, R1       ; tmp = a + b
        MOV   R0, R1       ; a = b
        MOV   R1, R4       ; b = tmp
        SUB   R2, R3       ; counter--
        JNZ   loop
        OUT   R0           ; prints 55
        HALT
