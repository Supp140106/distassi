// begin Ayushchandra
//  client.c
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "logger.h"
#include <time.h>

int main(int argc, char *argv[]) {
  int sock = -1;
  char current_server_ip[64] = "127.0.0.1";
  char port_str[16];
  sprintf(port_str, "%d", PORT);
  const char *port = port_str;
  logger_init("CLIENT", "client.log", 1);
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
  log_event(LOG_INFO, "event=task_compiled source=%s output=task.out", source_file);

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
      log_event(LOG_WARN, "event=server_unreachable ip=%s", current_server_ip);
      char discovered_ip[64];
      if (discover_server(discovered_ip, 5)) {
        strcpy(current_server_ip, discovered_ip);
        log_event(LOG_INFO, "event=server_discovered ip=%s", current_server_ip);
      }
      sleep(1);
      continue;
    }

    struct timespec submit_start;
    send(sock, &(int){SUBMIT}, sizeof(int), 0);
    send(sock, &size, sizeof(int), 0);
    send(sock, buffer, size, 0);
    clock_gettime(CLOCK_MONOTONIC, &submit_start);

    log_event(LOG_INFO, "event=task_submitted server_ip=%s task_size=%d",
              current_server_ip, size);

    int worker_id;
    int output_size;

    if (recv(sock, &worker_id, sizeof(int), 0) > 0) {
      if (recv(sock, &output_size, sizeof(int), 0) > 0) {
        char *output = malloc(output_size + 1);
        output[output_size] = '\0';
        if (recv_all(sock, output, output_size) == -1) {
          log_event(LOG_ERROR, "event=result_recv_failed server_ip=%s",
                    current_server_ip);
          free(output);
          close(sock);
          continue;
        } else {
          struct timespec submit_end;
          clock_gettime(CLOCK_MONOTONIC, &submit_end);
          double wait_ms =
              (submit_end.tv_sec - submit_start.tv_sec) * 1000.0 +
              (submit_end.tv_nsec - submit_start.tv_nsec) / 1e6;
          log_event(LOG_INFO,
                    "event=result_received worker_id=%d output_size=%d "
                    "round_trip_ms=%.1f",
                    worker_id, output_size, wait_ms);

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
      log_event(LOG_ERROR, "event=server_disconnected server_ip=%s",
                current_server_ip);
      close(sock);
      // Let the loop reiterate, it will auto-discover and reconnect
    }
  }

  free(buffer);
  close(sock);

  return 0;
}
// end Ayushchandra