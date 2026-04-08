/* tests/slow_task.c
 * Task: deliberately slow (5 s sleep) — useful for watching load-balancer
 *       spread tasks across workers when submitted alongside fast tasks.
 */
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main() {
  time_t t = time(NULL);
  printf("Slow task started at: %s", ctime(&t));
  sleep(5);
  t = time(NULL);
  printf("Slow task finished at: %s", ctime(&t));
  printf("Slept for 5 seconds — load-balancer test complete.\n");
  return 0;
}
