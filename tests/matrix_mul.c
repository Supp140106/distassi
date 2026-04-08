/* tests/matrix_mul.c
 * Task: 10x10 matrix multiplication (no sleep – pure compute)
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
  int A[10][10], B[10][10], C[10][10];
  srand((unsigned)time(NULL));

  for (int i = 0; i < 10; i++)
    for (int j = 0; j < 10; j++) {
      A[i][j] = rand() % 10;
      B[i][j] = rand() % 10;
      C[i][j] = 0;
    }

  for (int i = 0; i < 10; i++)
    for (int j = 0; j < 10; j++)
      for (int k = 0; k < 10; k++)
        C[i][j] += A[i][k] * B[k][j];

  printf("Matrix Multiplication Result (first row):\n");
  for (int j = 0; j < 10; j++)
    printf("%4d ", C[0][j]);
  printf("\n");
  return 0;
}
