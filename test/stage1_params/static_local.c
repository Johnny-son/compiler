int increment() {
    static int x = 0;
    x = x + 1;
    return x;
}

int main() {
    int a = increment();
    int b = increment();
    int c = increment();
    putint(a);
    putch(10);
    putint(b);
    putch(10);
    putint(c);
    putch(10);
    return 0;
}
