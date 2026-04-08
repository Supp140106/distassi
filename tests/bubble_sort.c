/* tests/bubble_sort.c
 * Task: sort 1,000 random integers with bubble sort and report stats
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 1000

int main() {
  int arr[N];
  srand((unsigned)time(NULL));
  for (int i = 0; i < N; i++)
    arr[i] = rand() % 10000;

  /* bubble sort */
  for (int i = 0; i < N - 1; i++)
    for (int j = 0; j < N - 1 - i; j++)
      if (arr[j] > arr[j + 1]) {
        int tmp   = arr[j];
        arr[j]    = arr[j + 1];
        arr[j + 1] = tmp;
      }

  printf("Bubble Sort of %d elements:\n", N);
  printf("  Min : %d\n", arr[0]);
  printf("  Max : %d\n", arr[N - 1]);
  printf("  Mid : %d\n", arr[N / 2]);
  return 0;
}
