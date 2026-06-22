/* iterative factorial, printed via the out() builtin */
int main(void) {
    int n = 5;
    int r = 1;
    while (n > 0) {
        r = r * n;
        n = n - 1;
    }
    out(r);
    return 0;
}
