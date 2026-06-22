/* M2: straight-line arithmetic (no control flow yet) compiled by mcc. */
int main(void) {
    int a = 6;
    int b = 7;
    int c = a * b + 8;     /* 50 */
    out(c);                /* 50 */
    out(c % 7);            /* 1  */
    out(c - a * b);        /* 8  */
    return c;
}
