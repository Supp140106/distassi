// begin Ayushchandra
//  client.c  —  submit one or more .c files as separate tasks
//
//  Usage:
//    ./client [server_ip [port [file1.c file2.c ...]]]
//
//  Examples:
//    ./client                              # submit task.c to 127.0.0.1:8080
//    ./client 192.168.1.5                  # submit task.c to given IP
//    ./client 192.168.1.5 8080 task.c      # explicit file
//    ./client 192.168.1.5 8080 tests/a.c tests/b.c tests/c.c
//      → compiles each file independently and submits each as its own task
//
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "logger.h"
#include <pthread.h>
#include <time.h>

/* ── per-task context passed to each thread ─────────────── */
typedef struct {
  char server_ip[64];
  char port[16];
  char source_file[512]; // original .c path
  int  task_index;       // 0-based
} TaskCtx;

/* ── compile + connect + submit + receive (one task) ─────── */
static void *submit_task(void *arg) {
  TaskCtx *ctx = (TaskCtx *)arg;

  /* ── 1. compile ──────────────────────────────────────── */
  // Each thread uses its own unique output binary to avoid collisions
  char out_bin[512];
  snprintf(out_bin, sizeof(out_bin), "task_%d_%d.out", ctx->task_index,
           getpid());

  char compile_cmd[2048];
  snprintf(compile_cmd, sizeof(compile_cmd), "gcc %s -o %s",
           ctx->source_file, out_bin);

  int ret = system(compile_cmd);
  if (ret != 0) {
    fprintf(stderr, "[task %d] Compilation FAILED for %s\n",
            ctx->task_index, ctx->source_file);
    free(ctx);
    return NULL;
  }
  log_event(LOG_INFO, "event=task_compiled index=%d source=%s binary=%s",
            ctx->task_index, ctx->source_file, out_bin);

  /* ── 2. read binary ──────────────────────────────────── */
  FILE *f = fopen(out_bin, "rb");
  if (!f) {
    fprintf(stderr, "[task %d] Cannot open compiled binary %s\n",
            ctx->task_index, out_bin);
    free(ctx);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  int size = (int)ftell(f);
  rewind(f);
  char *buffer = malloc(size);
  fread(buffer, 1, size, f);
  fclose(f);
  remove(out_bin); // clean up local binary

  /* ── 3. connect & submit (with auto-discovery retry) ─── */
  char current_ip[64];
  strncpy(current_ip, ctx->server_ip, sizeof(current_ip));

  int sock = -1;
  struct timespec submit_start;

  while (1) {
    sock = connect_to_server(current_ip, ctx->port);
    if (sock < 0) {
      log_event(LOG_WARN, "event=server_unreachable index=%d ip=%s",
                ctx->task_index, current_ip);
      char discovered_ip[64];
      if (discover_server(discovered_ip, 5)) {
        strncpy(current_ip, discovered_ip, sizeof(current_ip));
        log_event(LOG_INFO,
                  "event=server_discovered index=%d ip=%s",
                  ctx->task_index, current_ip);
      }
      sleep(1);
      continue;
    }

    /* send task */
    send(sock, &(int){SUBMIT}, sizeof(int), 0);
    send(sock, &size,           sizeof(int), 0);
    send(sock, buffer,          size,        0);
    clock_gettime(CLOCK_MONOTONIC, &submit_start);

    log_event(LOG_INFO,
              "event=task_submitted index=%d server_ip=%s task_size=%d "
              "source=%s",
              ctx->task_index, current_ip, size, ctx->source_file);

    /* ── 4. receive result ───────────────────────────── */
    int worker_id, output_size;

    if (recv(sock, &worker_id, sizeof(int), 0) > 0 &&
        recv(sock, &output_size, sizeof(int), 0) > 0) {

      char *output = malloc(output_size + 1);
      output[output_size] = '\0';

      if (recv_all(sock, output, output_size) == -1) {
        log_event(LOG_ERROR,
                  "event=result_recv_failed index=%d server_ip=%s",
                  ctx->task_index, current_ip);
        free(output);
        close(sock);
        free(buffer);
        free(ctx);
        return NULL;
      }

      struct timespec submit_end;
      clock_gettime(CLOCK_MONOTONIC, &submit_end);
      double wait_ms =
          (submit_end.tv_sec  - submit_start.tv_sec)  * 1000.0 +
          (submit_end.tv_nsec - submit_start.tv_nsec) / 1e6;

      log_event(LOG_INFO,
                "event=result_received index=%d worker_id=%d "
                "output_size=%d round_trip_ms=%.1f",
                ctx->task_index, worker_id, output_size, wait_ms);

      printf("\n╔══════════════════════════════════════════╗\n");
      printf("║  Task %-3d ─ %s\n", ctx->task_index, ctx->source_file);
      printf("║  Worker ID  : %d\n", worker_id);
      printf("║  Round-trip : %.1f ms\n", wait_ms);
      printf("╠══════════════════════════════════════════╣\n");
      printf("%s\n", output);
      printf("╚══════════════════════════════════════════╝\n");

      free(output);
      close(sock);
      break; // done
    } else {
      log_event(LOG_ERROR,
                "event=server_disconnected index=%d server_ip=%s",
                ctx->task_index, current_ip);
      close(sock);
      // retry
    }
  }

  free(buffer);
  free(ctx);
  return NULL;
}

/* ── main ─────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
  logger_init("CLIENT", "client.log", 1);

  char server_ip[64] = "127.0.0.1";
  char port[16];
  snprintf(port, sizeof(port), "%d", PORT);

  /* parse positional args: [ip [port [file1 file2 ...]]] */
  int files_start = 1; // argv index where file list begins

  if (argc >= 2) {
    strncpy(server_ip, argv[1], sizeof(server_ip));
    files_start = 2;
  }
  if (argc >= 3) {
    // detect whether argv[2] looks like a port number (all digits)
    int looks_like_port = 1;
    for (int i = 0; argv[2][i]; i++) {
      if (argv[2][i] < '0' || argv[2][i] > '9') {
        looks_like_port = 0;
        break;
      }
    }
    if (looks_like_port) {
      strncpy(port, argv[2], sizeof(port));
      files_start = 3;
    }
    // else: argv[2] is a .c file, keep files_start = 2
  }

  /* build file list */
  int num_files = argc - files_start;
  char **files  = argv + files_start;

  static const char *default_file = "task.c";
  if (num_files <= 0) {
    files     = (char **)&default_file;
    num_files = 1;
  }

  printf("Submitting %d task(s) to %s:%s\n", num_files, server_ip, port);

  /* spawn one thread per file */
  pthread_t *tids = malloc(num_files * sizeof(pthread_t));

  for (int i = 0; i < num_files; i++) {
    TaskCtx *ctx = malloc(sizeof(TaskCtx));
    strncpy(ctx->server_ip,    server_ip, sizeof(ctx->server_ip));
    strncpy(ctx->port,         port,      sizeof(ctx->port));
    strncpy(ctx->source_file,  files[i],  sizeof(ctx->source_file));
    ctx->task_index = i;

    if (pthread_create(&tids[i], NULL, submit_task, ctx) != 0) {
      perror("pthread_create failed");
      free(ctx);
      tids[i] = 0;
    }
  }

  /* wait for all tasks to finish */
  for (int i = 0; i < num_files; i++) {
    if (tids[i]) {
      pthread_join(tids[i], NULL);
    }
  }

  free(tids);
  printf("\nAll tasks completed.\n");
  return 0;
}
// end Ayushchandra
