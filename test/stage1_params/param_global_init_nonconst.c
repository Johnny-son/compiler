// @expected-error E1301 h at line 3
int g = 1;
int h = g + 1;

int main()
{
    return h;
}