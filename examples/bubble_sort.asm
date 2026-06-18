; bubble_sort.asm — sort a 5-element array in memory ascending, then print it.
; Demonstrates memory access, nested loops, and signed comparison branches.
;
; Register usage:
;   R0 = 1 (decrement)      R1 = 4 (element stride)   R2 = outer counter
;   R3 = element pointer     R4 = inner counter        R5 = x, R6 = y, R7 = x-y

        LOADI R0, 1
        LOADI R1, 4
        LOADI R2, 5
        SUB   R2, R0        ; outer = N - 1 = 4

outer:
        LOADI R3, arr       ; pointer to arr[0]
        MOV   R4, R2        ; inner count = outer

inner:
        LOAD  R5, [R3+0]    ; x = arr[j]
        LOAD  R6, [R3+4]    ; y = arr[j+1]
        MOV   R7, R5
        SUB   R7, R6        ; x - y  (sets flags)
        JG    doswap        ; if x > y, swap
        JMP   skip

doswap:
        STORE [R3+0], R6    ; arr[j]   = y
        STORE [R3+4], R5    ; arr[j+1] = x

skip:
        ADD   R3, R1        ; advance pointer
        SUB   R4, R0        ; inner--
        JNZ   inner
        SUB   R2, R0        ; outer--
        JNZ   outer

        ; print the sorted array
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
