; Nested loops: a short fixed-length inner loop (4) repeated 100 times.
; The inner branch follows a repeating T,T,T,N pattern -- learnable by a
; global-history predictor (gshare) but not by a per-PC bimodal counter.
        LOADI R1, 1
        LOADI R3, 100
outer:
        LOADI R2, 4
inner:
        SUB   R2, R1
        JNZ   inner
        SUB   R3, R1
        JNZ   outer
        HALT
