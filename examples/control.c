/* M3: if/else, for, comparisons, and short-circuit logical operators. */
int main(void) {
    int s = 0;
    int i;
    for (i = 1; i <= 5; i = i + 1) {
        if (i == 3) {
            s = s + 100;
        } else {
            s = s + i;
        }
    }
    out(s);          /* 1 + 2 + 100 + 4 + 5 = 112 */
    out(1 && 0);     /* 0 */
    out(2 || 0);     /* 1 */
    out(!0);         /* 1 */
    return s;
}
