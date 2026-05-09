int f() {
    static int cnt = 0;
    cnt = cnt + 1;
    return cnt;
}

int g() {
    static int cnt = 100;
    cnt = cnt + 1;
    return cnt;
}

int main() {
    putint(f());
    putch(10);
    putint(g());
    putch(10);
    putint(f());
    putch(10);
    putint(g());
    putch(10);
    return 0;
}
