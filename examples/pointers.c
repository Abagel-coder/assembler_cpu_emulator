/* M5: a global array passed (decayed to a pointer) into a function. */
int g[5];

int sum(int *p, int n) {
    int s;
    int i;
    s = 0;
    for (i = 0; i < n; i = i + 1) {
        s = s + p[i];
    }
    return s;
}

int main(void) {
    int i;
    for (i = 0; i < 5; i = i + 1) {
        g[i] = i + 1;      /* 1, 2, 3, 4, 5 */
    }
    out(sum(g, 5));        /* 15 */
    return 0;
}
