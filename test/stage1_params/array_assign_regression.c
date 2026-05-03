int main() {
    int a[10];
    int i = 0;

    while (i < 10) {
        a[i] = i * i;
        i++;
    }

    a[2] = a[8];
    putint(a[2]);
    putch(10);
    return a[0];
}
