/* tests/prime_sieve.c
 * Task: Sieve of Eratosthenes up to 100,000
 */
#include <stdio.h>
#include <stdlib.h>

#define LIMIT 100000

int main() {
  char *sieve = calloc(LIMIT + 1, 1);
  sieve[0] = sieve[1] = 1; // not prime

  for (int i = 2; (long long)i * i <= LIMIT; i++) {
    if (!sieve[i]) {
      for (int j = i * i; j <= LIMIT; j += i)
        sieve[j] = 1;
    }
  }

  int count = 0;
  int last  = 0;
  for (int i = 2; i <= LIMIT; i++) {
    if (!sieve[i]) {
      count++;
      last = i;
    }
  }

  printf("Primes up to %d: %d found\n", LIMIT, count);
  printf("Largest prime  : %d\n", last);
  free(sieve);
  return 0;
}
