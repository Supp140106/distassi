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

/* ── Worker TUI (Fixed header with scrolling logs) ──────── */

#define WRK_DASH_HEIGHT 10

static void setup_ui(void) {
  // Clear screen and set scrolling region
  printf("\033[2J");
  printf("\033[%d;r", WRK_DASH_HEIGHT + 1);
  printf("\033[H");
  printf("\033[%d;1H", WRK_DASH_HEIGHT + 1);
  fflush(stdout);
}

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

static void wrk_sep(const char *left, const char *mid, const char *right) {

  printf("%s", left);
  for (int i = 0; i < 46; i++) printf("%s", mid);
  printf("%s\n", right);
}

static void wrk_kv_col(const char *label, const char *ansi, const char *text) {
  printf("║  %-12s: %s%s\033[0m", label, ansi, text);
  int pad = 30 - (int)strlen(text);
  for (int i = 0; i < pad; i++) putchar(' ');
  printf("║\n");
}

static void wrk_kv(const char *label, const char *text) {
  printf("║  %-12s: %-30s║\n", label, text);
}


void *worker_dashboard_thread(void *arg) {
  (void)arg;
  while (1) {
    printf("\033[H\033[2J");

    wrk_sep("╔", "═", "╗");
    {
      char hdr[64];
      snprintf(hdr, sizeof(hdr), "   WORKER DASHBOARD  ─  PID %d", getpid());
      printf("║ %-45s║\n", hdr);
    }
    wrk_sep("╠", "═", "╣");

    wrk_kv("Server IP", current_server_ip);

    if (current_sock != -1 && worker_state != 0) {
      char buf;
      int r = recv(current_sock, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
      if (r == 0 || (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        worker_state = 3;
      }
    }

    static long long prev_idle = 0, prev_total = 0;
    long long idle, total;
    double cpu_usage = 0.0;
    if (get_cpu_idle_total(&idle, &total) == 0) {
      long long diff_idle = idle - prev_idle;
      long long diff_total = total - prev_total;
      if (diff_total > 0)
        cpu_usage = 100.0 * (1.0 - (double)diff_idle / diff_total);
      prev_idle = idle;
      prev_total = total;
    }

    double mem_usage = 0.0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
      char line[256];
      long long mt = 0, ma = 0;
      while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
          sscanf(line, "MemTotal: %lld kB", &mt);
        if (strncmp(line, "MemAvailable:", 13) == 0)
          sscanf(line, "MemAvailable: %lld kB", &ma);
      }
      fclose(fp);
      if (mt > 0)
        mem_usage = 100.0 * (1.0 - (double)ma / mt);
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

    char cbuf[16], mbuf[16];
    snprintf(cbuf, sizeof(cbuf), "%.1f%%", cpu_usage);
    snprintf(mbuf, sizeof(mbuf), "%.1f%%", mem_usage);
    wrk_kv_col("Sys CPU", (cpu_usage > 80.0) ? "\033[31m" : "\033[32m", cbuf);
    wrk_kv_col("Sys RAM", (mem_usage > 80.0) ? "\033[31m" : "\033[32m", mbuf);

    const char *st_color, *st_text;
    char duration_str[32] = "-";
    if (worker_state == 0) {
      st_color = "\033[31m";
      st_text = "RECONNECTING";
    } else if (worker_state == 1) {
      st_color = "\033[32m";
      st_text = "IDLE (Connected)";
    } else if (worker_state == 2) {
      st_color = "\033[33m";
      st_text = "WORKING";
      snprintf(duration_str, sizeof(duration_str), "%lds",
               time(NULL) - task_start_time);
    } else {
      st_color = "\033[31m";
      st_text = "SERVER OFFLINE";
    }

    wrk_kv_col("Status", st_color, st_text);
    wrk_kv("Duration", duration_str);

    wrk_sep("╠", "═", "╣");
    printf("║ Recent Events:                              ║\n");
    logger_lock();
    for (int i = 0; i < LOG_RING_SIZE; i++) {
      const char *line = logger_get_line(i);
      if (line[0] != '\0') {
        printf("║  %-44s║\n", line);
      }
    }
    logger_unlock();
    wrk_sep("╚", "═", "╝");

    fflush(stdout);
    sleep(1);
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  char worker_log[64];
  sprintf(worker_log, "worker_%d.log", getpid());
  logger_init("WORKER", worker_log, 1);


  sprintf(current_server_port, "%d", PORT);

  if (argc >= 2) {
    strcpy(current_server_ip, argv[1]);
  }
  if (argc >= 3) {
    strcpy(current_server_port, argv[2]);
  }

  // Setup the TUI with fixed header and scrolling region
  setup_ui();


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
          log_event(LOG_WARN,
                    "event=promoting_to_server reason=election_timeout");
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
      log_event(LOG_WARN,
                "event=server_connection_lost stage=recv_data task_size=%d",
                size);
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
    }
    pclose(pipe);

    clock_gettime(CLOCK_MONOTONIC, &exec_end);
    double exec_duration_ms = (exec_end.tv_sec - exec_start.tv_sec) * 1000.0 +
                              (exec_end.tv_nsec - exec_start.tv_nsec) / 1e6;
    log_event(LOG_INFO, "event=task_executed duration_ms=%.1f output_size=%d",
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
