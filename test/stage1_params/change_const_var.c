// @expected-error E1303 a at line 4
int main() {
    const int a = 10;
    a = 20;
    return 0;
}