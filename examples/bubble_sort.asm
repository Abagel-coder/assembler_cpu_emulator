; Bubble-sort a 5-element array in memory ascending, then print it.
; R0=1 R1=4(stride) R2=outer R3=ptr R4=inner R5=x R6=y R7=x-y
        LOADI R0, 1
        LOADI R1, 4
        LOADI R2, 5
        SUB   R2, R0

outer:
        LOADI R3, arr
        MOV   R4, R2

inner:
        LOAD  R5, [R3+0]
        LOAD  R6, [R3+4]
        MOV   R7, R5
        SUB   R7, R6
        JG    doswap
        JMP   skip

doswap:
        STORE [R3+0], R6
        STORE [R3+4], R5

skip:
        ADD   R3, R1
        SUB   R4, R0
        JNZ   inner
        SUB   R2, R0
        JNZ   outer

        LOADI R3, arr
        LOADI R4, 5
print:
        LOAD  R5, [R3+0]
        OUT   R5
        ADD   R3, R1
        SUB   R4, R0
        JNZ   print
        HALT

.org 256
arr:
.word 5
.word 1
.word 4
.word 2
.word 3
