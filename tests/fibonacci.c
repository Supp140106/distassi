/* tests/fibonacci.c
 * Task: compute Fibonacci numbers up to index 50 using memoisation
 */
#include <stdio.h>

#define N 50

int main() {
  unsigned long long fib[N + 1];
  fib[0] = 0;
  fib[1] = 1;
  for (int i = 2; i <= N; i++)
    fib[i] = fib[i - 1] + fib[i - 2];

  printf("Fibonacci sequence (F0 – F%d):\n", N);
  for (int i = 0; i <= N; i++)
    printf("  F%2d = %llu\n", i, fib[i]);

  return 0;
}
