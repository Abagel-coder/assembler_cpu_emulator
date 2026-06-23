/* M5: a local array, address-of an element, and a pointer write. */
int main(void) {
    int a[4];
    int i;
    int s;
    int *p;
    for (i = 0; i < 4; i = i + 1) {
        a[i] = i * 10;     /* 0, 10, 20, 30 */
    }
    p = &a[1];
    *p = 99;               /* a[1] = 99 */
    s = 0;
    for (i = 0; i < 4; i = i + 1) {
        s = s + a[i];      /* 0 + 99 + 20 + 30 = 149 */
    }
    out(s);
    return s;
}
