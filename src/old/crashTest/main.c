#include <stdio.h>

void testFunction() {
    int i = 0;
    i /= i;
    printf("0/0 = %i\n", i);
}

int32_t main() {
    printf("trying to divide by zero now...\n");
    testFunction();
    return 0;
}
