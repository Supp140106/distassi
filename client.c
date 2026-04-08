// begin Ayushchandra
//  client.c
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int main(int argc, char *argv[]) {
  int sock = -1;
  char current_server_ip[64] = "127.0.0.1";
  char port_str[16];
  sprintf(port_str, "%d", PORT);
  const char *port = port_str;
  char source_file[256] = "task.c";

  if (argc >= 2) {
    strcpy(current_server_ip, argv[1]);
  }
  if (argc >= 3) {
    port = argv[2];
  }
  if (argc >= 4) {
    strcpy(source_file, argv[3]);
  }

  char compile_cmd[512];
  sprintf(compile_cmd, "gcc %s -o task.out", source_file);
  system(compile_cmd);

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

  while (1) {
    sock = connect_to_server(current_server_ip, port);
    if (sock < 0) {
      printf("Server down. Waiting for UDP Broadcast Discovery...\n");
      char discovered_ip[64];
      if (discover_server(discovered_ip, 5)) {
        strcpy(current_server_ip, discovered_ip);
        printf("Discovered server at: %s\n", current_server_ip);
      }
      sleep(1);
      continue;
    }

    send(sock, &(int){SUBMIT}, sizeof(int), 0);
    send(sock, &size, sizeof(int), 0);
    send(sock, buffer, size, 0);

    printf("Task sent to server %s, waiting for execution...\n",
           current_server_ip);

    int worker_id;
    int output_size;

    if (recv(sock, &worker_id, sizeof(int), 0) > 0) {
      if (recv(sock, &output_size, sizeof(int), 0) > 0) {
        char *output = malloc(output_size + 1);
        output[output_size] = '\0';
        if (recv_all(sock, output, output_size) == -1) {
          printf("Error receiving full output from server. Retrying...\n");
          free(output);
          close(sock);
          continue;
        } else {
          printf("\n--- Task Execution Result ---\n");
          printf("Worker ID: %d\n", worker_id);
          printf("Output: %s\n", output);
          printf("-----------------------------\n");
          free(output);
          close(sock);
          break; // Success! Exit the infinite retry loop
        }
      }
    } else {
      printf("Server crashed while executing task. Auto-reconnecting...\n");
      close(sock);
      // Let the loop reiterate, it will auto-discover and reconnect
    }
  }

  free(buffer);
  close(sock);

  return 0;
}
// end Ayushchandra
