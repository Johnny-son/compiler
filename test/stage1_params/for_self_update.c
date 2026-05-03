int main() {
    int i = 0;
    int j = 0;
    int sum = 0;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            if (j == 1) {
                continue;
            }
            if (i == 3) {
                break;
            }
            sum = sum + i * 10 + j;
        }
    }

    for (i = 3; i > 0; i--) {
        sum = sum + i;
    }

    for (int k = 0, m = 0; k < 3; k++, m++) {
        sum = sum + k + m;
    }

    return sum;
}
