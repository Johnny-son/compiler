// @expected-front-error line 3:22 mismatched input ';' expecting {'[', '='}
int main() {
    static const int x;
    return 0;
}
