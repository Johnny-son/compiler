// @expected-error E1201 id at line 9
int id(int x)
{
    return x;
}

int main()
{
    return id(1, 2);
}