int main() {
    int x = 1;
    int a = x++;
    int b = ++x;
    int c = x--;
    int d = --x;

    putint(a);
    putch(32);
    putint(b);
    putch(32);
    putint(c);
    putch(32);
    putint(d);
    putch(10);
    return x;
}
