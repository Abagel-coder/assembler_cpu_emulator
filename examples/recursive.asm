; Recursive sum 1..n via CALL/RET (stresses the stack, returns, and the BTB).
        LOADI R0, 10
        CALL  sumrec
        OUT   R0             ; 55
        HALT

sumrec:                      ; R0 = n; returns sum(0..n) in R0
        LOADI R1, 0
        ADD   R0, R1         ; set Z if n == 0
        JZ    base
        PUSH  R0             ; save n
        LOADI R1, 1
        SUB   R0, R1         ; n - 1
        CALL  sumrec
        POP   R1             ; restore n
        ADD   R0, R1         ; sum(n-1) + n
        RET
base:
        LOADI R0, 0
        RET
