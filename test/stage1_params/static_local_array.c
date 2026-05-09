int sum() {
    static int arr[3] = {10, 20, 30};
    int s = arr[0] + arr[1] + arr[2];
    arr[0] = arr[0] + 1;
    return s;
}

int main() {
    int a = sum();
    int b = sum();
    putint(a);
    putch(10);
    putint(b);
    putch(10);
    return 0;
}
