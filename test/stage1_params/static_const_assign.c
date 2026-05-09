// @expected-error E1303 x at line 4
int main() {
    static const int x = 10;
    x = 20;
    return 0;
}
