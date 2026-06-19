; Cache-stress: build A[0..N-1]=i, then sum the array over 3 passes.
; The 1600-byte working set exceeds a small L1, so a size sweep shows the
; capacity cliff (re-misses every pass vs. fitting after the first).
        LOADI R1, 1
        LOADI R2, 4          ; word stride
        LOADI R5, 400        ; N

        LOADI R6, 0          ; i
        LOADI R3, arr
init:
        STORE [R3+0], R6
        ADD   R3, R2
        ADD   R6, R1
        MOV   R4, R6
        SUB   R4, R5
        JNZ   init

        LOADI R0, 0          ; sum
        LOADI R7, 3          ; passes
pass:
        LOADI R3, arr
        LOADI R6, 0
sum:
        LOAD  R4, [R3+0]
        ADD   R0, R4
        ADD   R3, R2
        ADD   R6, R1
        MOV   R4, R6
        SUB   R4, R5
        JNZ   sum
        SUB   R7, R1
        JNZ   pass
        OUT   R0             ; 3 * (399*400/2) = 239400
        HALT

.org 4096
arr:
