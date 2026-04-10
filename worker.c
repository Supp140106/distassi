// begin dheerajpateru
//  worker.c
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "logger.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

char current_server_ip[64] = "127.0.0.1";
char current_server_port[16]; // global port str
int worker_state =
    0; // 0 = Reconnecting/Election, 1 = Idle, 2 = Working, 3 = Server Offline
time_t task_start_time = 0;
int current_sock = -1;

int get_cpu_idle_total(long long *idle, long long *total) {
  FILE *fp = fopen("/proc/stat", "r");
  if (!fp)
    return -1;
  char buffer[1024];
  if (fgets(buffer, sizeof(buffer), fp) == NULL) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  long long user, nice, system, idl, iowait, irq, softirq, steal, guest,
      guest_nice;
  if (sscanf(buffer, "cpu %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
             &user, &nice, &system, &idl, &iowait, &irq, &softirq, &steal,
             &guest, &guest_nice) < 10) {
    return -1;
  }
  *idle = idl + iowait;
  *total = user + nice + system + idl + iowait + irq + softirq + steal;
  return 0;
}

void *worker_dashboard_thread(void *arg) {
  (void)arg;
  while (1) {
    printf("\033[?25l\033[H\033[J");
    printf("====================================================\n");
    printf("  WORKER DASHBOARD (Worker ID: %d)\n", getpid());
    printf("====================================================\n");
    printf("Server IP: %s\n", current_server_ip);

    if (current_sock != -1 && worker_state != 0) {
      char buf;
      int r = recv(current_sock, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
      if (r == 0 || (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        worker_state = 3; // Server Offline
      }
    }

    static long long prev_idle = 0, prev_total = 0;
    long long idle, total;
    double cpu_usage = 0.0;
    if (get_cpu_idle_total(&idle, &total) == 0) {
      long long diff_idle = idle - prev_idle;
      long long diff_total = total - prev_total;
      if (diff_total > 0) {
        cpu_usage = 100.0 * (1.0 - (double)diff_idle / diff_total);
      }
      prev_idle = idle;
      prev_total = total;
    }

    double mem_usage = 0.0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
      char line[256];
      long long mem_total = 0, mem_available = 0;
      while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
          sscanf(line, "MemTotal: %lld kB", &mem_total);
        if (strncmp(line, "MemAvailable:", 13) == 0)
          sscanf(line, "MemAvailable: %lld kB", &mem_available);
      }
      fclose(fp);
      if (mem_total > 0) {
        mem_usage = 100.0 * (1.0 - (double)mem_available / mem_total);
      }
    }

    if (current_sock != -1 && worker_state != 0 && worker_state != 3 &&
        current_server_port[0] != '\0') {
      int stat_sock = connect_to_server(current_server_ip, current_server_port);
      if (stat_sock >= 0) {
        int type = SEND_STATS;
        int my_id = getpid();
        send(stat_sock, &type, sizeof(int), 0);
        send(stat_sock, &my_id, sizeof(int), 0);
        send(stat_sock, &cpu_usage, sizeof(double), 0);
        send(stat_sock, &mem_usage, sizeof(double), 0);
        close(stat_sock);
      }
    }

    char status_str[64];
    char duration_str[64] = "-";
    if (worker_state == 0) {
      sprintf(status_str, "\033[31mRECONNECTING / ELECTION\033[0m");
    } else if (worker_state == 1) {
      sprintf(status_str, "\033[32mIDLE (Connected)\033[0m");
    } else if (worker_state == 2) {
      sprintf(status_str, "\033[33mWORKING\033[0m");
      time_t now = time(NULL);
      sprintf(duration_str, "%lds", now - task_start_time);
    } else if (worker_state == 3) {
      sprintf(status_str, "\033[31mSERVER OFFLINE (Task will fail)\033[0m");
    }
    printf("Status:    %s\n", status_str);
    printf("Duration:  %s\n", duration_str);
    printf("Sys CPU:   %.1f%%\n", cpu_usage);
    printf("Sys RAM:   %.1f%%\n", mem_usage);
    printf("====================================================\n");
    printf("Recent Events:\n");
    logger_lock();
    for (int i = 0; i < LOG_RING_SIZE; i++) {
      const char *line = logger_get_line(i);
      if (line[0] != '\0') {
        printf("  %s\n", line);
      }
    }
    logger_unlock();
    sleep(1);
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  char worker_log[64];
  sprintf(worker_log, "worker_%d.log", getpid());
  logger_init("WORKER", worker_log, 0);

  sprintf(current_server_port, "%d", PORT);

  if (argc >= 2) {
    strcpy(current_server_ip, argv[1]);
  }
  if (argc >= 3) {
    strcpy(current_server_port, argv[2]);
  }

  pthread_t dash_tid;
  pthread_create(&dash_tid, NULL, worker_dashboard_thread, NULL);
  pthread_detach(dash_tid);

  while (1) {
    if (current_sock < 0) {
      worker_state = 0; // Reconnecting
      current_sock = connect_to_server(current_server_ip, current_server_port);
      if (current_sock < 0) {
        srand(time(NULL) ^ getpid());
        int delay = (rand() % 3) + 1; // 1 to 3 seconds

        char discovered_ip[64];
        if (discover_server(discovered_ip, delay)) {
          strcpy(current_server_ip, discovered_ip);
          log_event(LOG_INFO, "event=server_discovered ip=%s", discovered_ip);
        } else {
          // Timer expired without hearing a server => I BECOME the new server!
          log_event(LOG_WARN, "event=promoting_to_server reason=election_timeout");
          execl("./server", "./server", NULL);
          // If execl returns, it failed
          perror("execl failed to spawn server");
          exit(1);
        }
        continue;
      }
      int req_type = REQUEST_TASK;
      int my_id = getpid();
      send(current_sock, &req_type, sizeof(int), 0);
      send(current_sock, &my_id, sizeof(int), 0);
      worker_state = 1; // IDLE
      log_event(LOG_INFO, "event=connected_to_server ip=%s worker_id=%d",
                current_server_ip, my_id);
    }

    int size;
    if (recv(current_sock, &size, sizeof(int), 0) <= 0) {
      log_event(LOG_WARN, "event=server_connection_lost stage=recv_size");
      close(current_sock);
      current_sock = -1;
      continue;
    }

    char *buffer = malloc(size);
    if (recv_all(current_sock, buffer, size) == -1) {
      log_event(LOG_WARN, "event=server_connection_lost stage=recv_data "
                "task_size=%d", size);
      free(buffer);
      close(current_sock);
      current_sock = -1;
      continue;
    }

    worker_state = 2; // WORKING
    task_start_time = time(NULL);
    log_event(LOG_INFO, "event=task_received task_size=%d", size);
    // end dheerajpateru
    // begin pallavi

    char filename[256];
    sprintf(filename, "received_task_%d.out", getpid());
    FILE *f = fopen(filename, "wb");
    fwrite(buffer, 1, size, f);
    fclose(f);

    chmod(filename, 0755);

    log_event(LOG_INFO, "event=task_executing filename=%s task_size=%d",
              filename, size);

    struct timespec exec_start, exec_end;
    clock_gettime(CLOCK_MONOTONIC, &exec_start);

    char cmd[512];
    sprintf(cmd, "./%s", filename);
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
      log_event(LOG_ERROR, "event=popen_failed filename=%s", filename);
      perror("popen failed");
      close(current_sock);
      current_sock = -1;
      free(buffer);
      continue;
    }

    char output_buffer[4096] = {0};
    int total_read = 0;
    while (fgets(output_buffer + total_read, sizeof(output_buffer) - total_read,
                 pipe) != NULL) {
      total_read = strlen(output_buffer);

      // We can also periodically check connection status while working
      if (worker_state == 3) {
        // You could optionally break here to kill the task early,
        // or just continue and let the send fail later.
      }
    }
    pclose(pipe);

    clock_gettime(CLOCK_MONOTONIC, &exec_end);
    double exec_duration_ms = (exec_end.tv_sec - exec_start.tv_sec) * 1000.0 +
                              (exec_end.tv_nsec - exec_start.tv_nsec) / 1e6;
    log_event(LOG_INFO,
              "event=task_executed duration_ms=%.1f output_size=%d",
              exec_duration_ms, total_read);

    int worker_id = getpid();
    if (send(current_sock, &worker_id, sizeof(int), 0) <= 0 ||
        send(current_sock, &total_read, sizeof(int), 0) <= 0 ||
        send(current_sock, output_buffer, total_read, 0) <= 0) {
      log_event(LOG_ERROR,
                "event=result_send_failed worker_id=%d output_size=%d",
                worker_id, total_read);
      close(current_sock);
      current_sock = -1;
      free(buffer);
      remove(filename);
      continue;
    }

    long total_elapsed = (long)(time(NULL) - task_start_time);
    log_event(LOG_INFO,
              "event=result_sent worker_id=%d output_size=%d "
              "total_sec=%ld exec_ms=%.1f",
              worker_id, total_read, total_elapsed, exec_duration_ms);

    free(buffer);
    remove(filename);
    worker_state = 1; // Back to IDLE
    // Loop back to wait for the next task on the same socket
  }

  return 0;
}
// end pallavi