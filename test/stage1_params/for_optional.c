int main()
{
    int i;
    int sum;
    i = 0;
    sum = 0;

    for (;;) {
        i = i + 1;
        if (i > 4) {
            break;
        }
        sum = sum + i;
    }

    return sum;
}
