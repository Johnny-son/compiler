// @expected-error E1400 a at line 3
// @expected-error E1112 bad,a at line 3
int bad(int a, int a)
{
    return a;
}

int main()
{
    return bad(1, 2);
}
