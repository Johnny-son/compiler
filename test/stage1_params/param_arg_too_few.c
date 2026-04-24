// @expected-error E1201 add2 at line 9
int add2(int a, int b)
{
    return a + b;
}

int main()
{
    return add2(1);
}