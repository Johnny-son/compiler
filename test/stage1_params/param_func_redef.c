// @expected-error E1102 foo at line 7
int foo(int a)
{
    return a;
}

int foo(int b)
{
    return b + 1;
}

int main()
{
    return foo(1);
}