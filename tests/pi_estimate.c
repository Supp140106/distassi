/* tests/pi_estimate.c
 * Task: estimate π using the Leibniz formula (1M iterations)
 */
#include <stdio.h>

#define ITERATIONS 1000000

int main() {
  double pi = 0.0;
  for (int i = 0; i < ITERATIONS; i++) {
    double term = 1.0 / (2 * i + 1);
    if (i % 2 == 0)
      pi += term;
    else
      pi -= term;
  }
  pi *= 4.0;

  printf("Pi Estimation (Leibniz, %d terms):\n", ITERATIONS);
  printf("  pi ≈ %.10f\n", pi);
  printf("  err = %.2e\n", pi - 3.14159265358979323846);
  return 0;
}
