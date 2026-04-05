// client.c
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int main(int argc, char *argv[]) {
  const char *host = "127.0.0.1";
  char port_str[16];
  sprintf(port_str, "%d", PORT);
  const char *port = port_str;

  if (argc == 2) {
    host = argv[1];
  } else if (argc >= 3) {
    host = argv[1];
    port = argv[2];
  } else if (argc == 1) {
    printf("Usage: %s <server_ip> [port]\n", argv[0]);
    printf("Defaulting to %s:%s\n\n", host, port);
  }

  system("gcc task.c -o task.out");

  FILE *f = fopen("task.out", "rb");
  if (!f) {
    perror("File open error");
    return 1;
  }

  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  rewind(f);

  char *buffer = malloc(size);
  fread(buffer, 1, size, f);
  fclose(f);

  int sock = connect_to_server(host, port);
  if (sock < 0) {
    printf("Failed to connect to server %s:%s\n", host, port);
    free(buffer);
    return 1;
  }

  send(sock, &(int){SUBMIT}, sizeof(int), 0);
  send(sock, &size, sizeof(int), 0);
  send(sock, buffer, size, 0);

  printf("Task sent to server, waiting for execution...\n");

  int worker_id;
  int output_size;

  if (recv(sock, &worker_id, sizeof(int), 0) > 0) {
    if (recv(sock, &output_size, sizeof(int), 0) > 0) {
      char *output = malloc(output_size + 1);
      output[output_size] = '\0';
      if (recv_all(sock, output, output_size) == -1) {
        printf("Error receiving full output from server.\n");
      } else {
        printf("\n--- Task Execution Result ---\n");
        printf("Worker ID: %d\n", worker_id);
        printf("Output: %s\n", output);
        printf("-----------------------------\n");
      }
      free(output);
    }
  } else {
    printf("Failed to receive result from server.\n");
  }

  free(buffer);
  close(sock);

  return 0;
}
