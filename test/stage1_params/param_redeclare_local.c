// @expected-error E1400 x at line -1
int main() {
    int x = 1;
    int x = 2;
    return x;
}