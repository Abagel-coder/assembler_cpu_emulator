; Memory-mapped I/O: print two values through the console device, print a
; newline character, then halt via the MMIO halt register.
        LOADI R1, 0xE000     ; MMIO_OUT  (store -> print decimal)
        LOADI R2, 0xE004     ; MMIO_OUTC (store -> print low byte as a char)
        LOADI R3, 0xE00C     ; MMIO_HALT (store -> halt)

        LOADI R0, 42
        STORE [R1+0], R0     ; prints 42
        LOADI R0, 7
        STORE [R1+0], R0     ; prints 7
        LOADI R0, 65
        STORE [R2+0], R0     ; prints 'A'
        LOADI R0, 10
        STORE [R2+0], R0     ; prints newline

        STORE [R3+0], R0     ; halt
        HALT                 ; backstop (not reached)
