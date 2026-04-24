int id(int x)
{
    return x;
}

int add3(int a, int b, int c)
{
    return a + b + c;
}

int main()
{
    return add3(id(1), id(2), id(3));
}
