/* tests/word_count.c
 * Task: count characters, words, and lines in a fixed Lorem Ipsum paragraph
 */
#include <ctype.h>
#include <stdio.h>

static const char *TEXT =
    "Lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua Ut enim ad minim "
    "veniam quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat Duis aute irure dolor in reprehenderit in voluptate "
    "velit esse cillum dolore eu fugiat nulla pariatur Excepteur sint "
    "occaecat cupidatat non proident sunt in culpa qui officia deserunt "
    "mollit anim id est laborum";

int main() {
  int chars = 0, words = 0, lines = 1;
  int in_word = 0;

  for (int i = 0; TEXT[i]; i++) {
    chars++;
    if (TEXT[i] == '\n') lines++;
    if (isspace((unsigned char)TEXT[i])) {
      in_word = 0;
    } else if (!in_word) {
      in_word = 1;
      words++;
    }
  }

  printf("Word Count Task:\n");
  printf("  Characters : %d\n", chars);
  printf("  Words      : %d\n", words);
  printf("  Lines      : %d\n", lines);
  return 0;
}
