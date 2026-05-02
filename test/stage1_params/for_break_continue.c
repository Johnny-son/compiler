int main()
{
    int i;
    int sum;
    i = 0;
    sum = 0;

    for (; i < 10; i = i + 1) {
        if (i == 2) {
            continue;
        }
        if (i == 5) {
            break;
        }
        sum = sum + i;
    }

    return sum;
}
