#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main() {

  int A[10][10], B[10][10], result[10][10];
  sleep(10);

  srand(time(0));

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      A[i][j] = rand() % 10;
      B[i][j] = rand() % 10;
    }
  }

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      result[i][j] = 0;
    }
  }

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      for (int k = 0; k < 10; k++) {
        result[i][j] += A[i][k] * B[k][j];
      }
    }
  }

  printf("Matrix A:\n");
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      printf("%d ", A[i][j]);
    }
    printf("\n");
  }

  printf("\nMatrix B:\n");
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      printf("%d ", B[i][j]);
    }
    printf("\n");
  }

  printf("\nResult matrix:\n");
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      printf("%d ", result[i][j]);
    }
    printf("\n");
  }

  return 0;
}
