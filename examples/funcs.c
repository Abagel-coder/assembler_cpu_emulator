/* M4: functions with parameters and nested calls. */
int sq(int x) {
    return x * x;
}

int add(int a, int b) {
    return a + b;
}

int main(void) {
    int r = add(sq(3), sq(4));   /* 9 + 16 = 25 */
    out(r);
    return r;
}
