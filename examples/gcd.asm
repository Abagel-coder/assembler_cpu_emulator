; Subtractive Euclidean GCD -- data-dependent branches (good predictor stress).
        LOADI R0, 48
        LOADI R1, 36
gcd:
        MOV   R2, R0
        SUB   R2, R1         ; a - b
        JZ    done
        JG    agt
        SUB   R1, R0         ; b -= a
        JMP   gcd
agt:
        SUB   R0, R1         ; a -= b
        JMP   gcd
done:
        OUT   R0             ; gcd(48,36) = 12
        HALT
