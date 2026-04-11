#include <stdio.h>
#include <time.h>

int main() {
    clock_t start_time = clock();
    double elapsed = 0;

    printf("Processing...\n");

    // Loop until ~5 seconds have passed
    while (elapsed < 5.0) {
        elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
    }

    printf("Done! Took approximately %.2f seconds.\n", elapsed);
    return 0;
}
