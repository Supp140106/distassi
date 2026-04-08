// begin Ranjith
//  server.c
#include "common.h"
#include "logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Task {
  int size;
  char *data;
  int client_sock;
  struct Task *next;
} Task;

typedef struct Worker {
  int sock;
  char client_ip[INET_ADDRSTRLEN];
  int worker_id;
  int status; // 0 = IDLE, 1 = WORKING
  time_t start_time;

  // Raw / smoothed resource metrics
  double cpu_usage;     // raw latest CPU%  (updated by SEND_STATS)
  double cpu_ema;       // exponential moving average of CPU (alpha=0.4)
  double memory_usage;  // raw latest RAM%

  int tasks_dispatched; // total tasks ever dispatched to this worker

  struct Worker *next;     // For idle queue
  struct Worker *all_next; // For global list

  pthread_mutex_t lock;
  pthread_cond_t cond;
  Task *assigned_task;
} Worker;

Worker *all_workers_head = NULL;
pthread_mutex_t all_workers_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── helpers ──────────────────────────────────────────── */

void add_worker_global(Worker *w) {
  pthread_mutex_lock(&all_workers_lock);
  w->all_next = all_workers_head;
  all_workers_head = w;
  pthread_mutex_unlock(&all_workers_lock);
}

void remove_worker_global(Worker *w) {
  pthread_mutex_lock(&all_workers_lock);
  Worker **curr = &all_workers_head;
  while (*curr) {
    if (*curr == w) {
      *curr = w->all_next;
      break;
    }
    curr = &(*curr)->all_next;
  }
  pthread_mutex_unlock(&all_workers_lock);
}

/* ── dashboard (Fixed header with scrolling logs) ──── */

static int g_srv_dash_height = 8; // default minimum

static void setup_ui(int height) {
  g_srv_dash_height = height;
  // Clear screen and set scrolling region
  printf("\033[2J\033[H"); 
  printf("\033[%d;r", g_srv_dash_height + 1);
  printf("\033[%d;1H", g_srv_dash_height + 1);
  fflush(stdout);
}



static void srv_sep(const char *left, const char *mid, const char *right) {
  printf("%s", left);
  for (int i = 0; i < 78; i++) printf("%s", mid);
  printf("%s\n", right);
}

static void srv_colored(const char *color, const char *text, int vis_w) {
  printf("%s%s\033[0m", color, text);
  int pad = vis_w - (int)strlen(text);
  for (int i = 0; i < pad; i++) putchar(' ');
}

static void srv_num(double val, int vis_w, double warn) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f", val);
  if (val >= warn) printf("\033[31m%s\033[0m", buf);
  else             printf("\033[32m%s\033[0m", buf);
  int pad = vis_w - (int)strlen(buf);
  for (int i = 0; i < pad; i++) putchar(' ');
}


void *dashboard_thread(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&all_workers_lock);

    // Save cursor, home, and draw dashboard
    printf("\033[s\033[H");

    /* ── header ── */
    srv_sep("╔", "═", "╗");
    {
      char hdr[80];
      snprintf(hdr, sizeof(hdr),
               "  SERVER DASHBOARD  -  UDP Discovery Port %d",
               UDP_DISCOVERY_PORT);
      printf("║\033[1;36m%-78s\033[0m║\n", hdr);
    }
    srv_sep("╠", "═", "╣");

    /* ── column header ── */
    printf("║ \033[1m%-15s\033[0m │ \033[1m%-7s\033[0m │ \033[1m%-8s\033[0m │ \033[1m%-8s\033[0m │ \033[1m%-12s\033[0m │ \033[1m%-11s\033[0m ║\n",
           "Worker IP", "ID", "CPU(%)", "RAM(%)", "Status", "Duration");
    srv_sep("╟", "─", "╢");

    /* ── worker rows ── */
    Worker *curr = all_workers_head;
    int count = 0;
    while (curr) {
        count++;
        curr = curr->all_next;
    }

    // Check if we need to expand the UI
    int needed_height = count + 6;
    if (needed_height < 8) needed_height = 8;
    if (needed_height != g_srv_dash_height) {
        setup_ui(needed_height);
        printf("\033[s\033[H"); // Re-set cursor after setup_ui
        // Re-draw border since setup_ui cleared it
        srv_sep("╔", "═", "╗");
        printf("║\033[1;36m  SERVER DASHBOARD  -  UDP Discovery Port %d\033[0m║\n", UDP_DISCOVERY_PORT);
        srv_sep("╠", "═", "╣");
    }

    /* ── column header ── */
    printf("\033[4;1H║ \033[1m%-15s\033[0m │ \033[1m%-7s\033[0m │ \033[1m%-8s\033[0m │ \033[1m%-8s\033[0m │ \033[1m%-12s\033[0m │ \033[1m%-11s\033[0m ║\n",
           "Worker IP", "ID", "CPU(%)", "RAM(%)", "Status", "Duration");
    srv_sep("╟", "─", "╢");

    curr = all_workers_head;
    int row = 0;
    while (curr) {
      char chk;
      int r = recv(curr->sock, &chk, 1, MSG_PEEK | MSG_DONTWAIT);
      if (r == 0 || (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK))
        curr->status = -1;

      if (curr->status != -1) {
        row++;
        char dur[16] = "-";
        const char *st_color;
        const char *st_text;
        if (curr->status == 0) {
          st_color = "\033[32m"; st_text = "IDLE";
        } else {
          st_color = "\033[33m"; st_text = "WORKING";
          snprintf(dur, sizeof(dur), "%lds", time(NULL) - curr->start_time);
        }

        printf("║ %-15s │ %-7d │ ", curr->client_ip, curr->worker_id);
        srv_num(curr->cpu_ema,       8, 80.0);  printf(" │ ");
        srv_num(curr->memory_usage,  8, 80.0);  printf(" │ ");
        srv_colored(st_color, st_text, 12);      printf(" │ ");
        printf("%-11s ║\n", dur);
      }
      curr = curr->all_next;
    }




    /* ── footer ── */
    srv_sep("╠", "═", "╣");
    {
      char tot[80];
      snprintf(tot, sizeof(tot), "  Total Workers: %-4d | System Active ", count);
      printf("║%-78s║\n", tot);
    }
    srv_sep("╚", "═", "╝");

    // Restore cursor to the logging area
    printf("\033[u");
    fflush(stdout);
    pthread_mutex_unlock(&all_workers_lock);
    sleep(1);
  }
  return NULL;
}


/* ── task / worker queues ─────────────────────────────── */
// end Ranjith
// begin sahanasri

typedef struct {
  Task *front;
  Task *rear;
} TaskQueue;

typedef struct {
  Worker *front;
  Worker *rear;
} WorkerQueue;

TaskQueue task_queue;
WorkerQueue idle_workers;
pthread_mutex_t dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dispatch_cond = PTHREAD_COND_INITIALIZER;

void init_queues() {
  task_queue.front = task_queue.rear = NULL;
  idle_workers.front = idle_workers.rear = NULL;
}

void enqueue_task(Task *task) {
  pthread_mutex_lock(&dispatch_lock);
  task->next = NULL;
  if (task_queue.rear == NULL) {
    task_queue.front = task_queue.rear = task;
  } else {
    task_queue.rear->next = task;
    task_queue.rear = task;
  }
  pthread_cond_signal(&dispatch_cond);
  pthread_mutex_unlock(&dispatch_lock);
}

void enqueue_task_front(Task *task) {
  pthread_mutex_lock(&dispatch_lock);
  task->next = task_queue.front;
  task_queue.front = task;
  if (task_queue.rear == NULL) {
    task_queue.rear = task;
  }
  pthread_cond_signal(&dispatch_cond);
  pthread_mutex_unlock(&dispatch_lock);
}

void enqueue_worker(Worker *w) {
  pthread_mutex_lock(&dispatch_lock);
  w->next = NULL;
  if (idle_workers.rear == NULL) {
    idle_workers.front = idle_workers.rear = w;
  } else {
    idle_workers.rear->next = w;
    idle_workers.rear = w;
  }
  pthread_cond_signal(&dispatch_cond);
  pthread_mutex_unlock(&dispatch_lock);
}

/*
 * ── Improved load-balancing dispatcher ──────────────────────────────────────
 *
 * Score formula (lower = better, pick minimum):
 *
 *   base   = 0.6 * cpu_ema  +  0.4 * ram%
 *
 *   saturation penalty:
 *     if cpu_ema  > 80  →  base += (cpu_ema  - 80) * 2.0
 *     if ram%     > 80  →  base += (ram%     - 80) * 1.5
 *
 *   fairness tiebreak:
 *     score = base + tasks_dispatched * 0.5
 *
 * The EMA (alpha = 0.4) is updated each time we receive a SEND_STATS packet,
 * giving a smooth signal that dampens short CPU spikes while still reacting
 * quickly to sustained load.
 */
#define EMA_ALPHA 0.4

static double load_score(const Worker *w) {
  double base = 0.6 * w->cpu_ema + 0.4 * w->memory_usage;

  // Saturation penalty — aggressively avoid workers near the wall
  if (w->cpu_ema > 80.0)
    base += (w->cpu_ema - 80.0) * 2.0;
  if (w->memory_usage > 80.0)
    base += (w->memory_usage - 80.0) * 1.5;

  // Fairness: slightly prefer workers that have handled fewer tasks overall
  base += w->tasks_dispatched * 0.5;

  return base;
}

void *dispatcher_thread(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&dispatch_lock);
    while (task_queue.front == NULL || idle_workers.front == NULL) {
      pthread_cond_wait(&dispatch_cond, &dispatch_lock);
    }

    // Dequeue oldest task (FIFO — fair to clients)
    Task *task = task_queue.front;
    task_queue.front = task_queue.front->next;
    if (task_queue.front == NULL)
      task_queue.rear = NULL;

    // --- Least-Loaded Worker Selection ---
    Worker *best = NULL;
    Worker *best_prev = NULL;
    double best_score = 1e18;

    Worker *prev = NULL;
    Worker *curr = idle_workers.front;
    while (curr) {
      double s = load_score(curr);
      if (s < best_score) {
        best_score = s;
        best = curr;
        best_prev = prev;
      }
      prev = curr;
      curr = curr->next;
    }

    // Remove chosen worker from the idle queue
    if (best_prev == NULL) {
      idle_workers.front = best->next;
    } else {
      best_prev->next = best->next;
    }
    if (idle_workers.rear == best) {
      idle_workers.rear = best_prev;
    }
    best->next = NULL;

    best->tasks_dispatched++;

    Worker *worker = best;

    pthread_mutex_lock(&worker->lock);
    worker->assigned_task = task;
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->lock);

    log_event(LOG_INFO,
              "event=task_dispatched worker_id=%d task_size=%d "
              "score=%.1f cpu_ema=%.1f ram=%.1f dispatched_total=%d",
              worker->worker_id, task->size, best_score, worker->cpu_ema,
              worker->memory_usage, worker->tasks_dispatched);

    pthread_mutex_unlock(&dispatch_lock);
  }
  return NULL;
}
// end sahanasri
// begin saikrishna

void handle_connection(int client_sock, struct sockaddr_in client_addr) {
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

  int type;
  if (recv(client_sock, &type, sizeof(int), 0) <= 0) {
    close(client_sock);
    return;
  }

  if (type == SUBMIT) {
    int size;
    if (recv(client_sock, &size, sizeof(int), 0) <= 0) {
      close(client_sock);
      return;
    }

    char *data = malloc(size);
    if (recv_all(client_sock, data, size) == -1) {
      free(data);
      close(client_sock);
      return;
    }

    Task *task = malloc(sizeof(Task));
    task->size = size;
    task->data = data;
    task->client_sock = client_sock;

    enqueue_task(task);
    log_event(LOG_INFO, "event=task_queued client_ip=%s task_size=%d",
              client_ip, size);
    // Do not close client_sock here, we need it to send the result later
  }
  // end saikrishna
  // begin Vamsi

  else if (type == SEND_STATS) {
    int worker_id;
    double cpu, mem;
    if (recv(client_sock, &worker_id, sizeof(int), 0) > 0 &&
        recv(client_sock, &cpu, sizeof(double), 0) > 0 &&
        recv(client_sock, &mem, sizeof(double), 0) > 0) {

      pthread_mutex_lock(&all_workers_lock);
      Worker *curr = all_workers_head;
      while (curr) {
        if (curr->worker_id == worker_id) {
          curr->cpu_usage    = cpu;
          curr->memory_usage = mem;
          // Update EMA: new_ema = alpha * raw + (1-alpha) * old_ema
          curr->cpu_ema = EMA_ALPHA * cpu + (1.0 - EMA_ALPHA) * curr->cpu_ema;
          break;
        }
        curr = curr->all_next;
      }
      pthread_mutex_unlock(&all_workers_lock);
    }
    close(client_sock);
  } else if (type == REQUEST_TASK) {
    int worker_id = -1;
    if (recv(client_sock, &worker_id, sizeof(int), 0) <= 0) {
      close(client_sock);
      return;
    }

    Worker my_worker;
    my_worker.sock = client_sock;
    strcpy(my_worker.client_ip, client_ip);
    my_worker.worker_id = worker_id;
    my_worker.status = 0;
    my_worker.cpu_usage = 0.0;
    my_worker.cpu_ema = 0.0;
    my_worker.memory_usage = 0.0;
    my_worker.tasks_dispatched = 0;
    my_worker.assigned_task = NULL;
    pthread_mutex_init(&my_worker.lock, NULL);
    pthread_cond_init(&my_worker.cond, NULL);

    add_worker_global(&my_worker);
    log_event(LOG_INFO, "event=worker_connected worker_id=%d ip=%s", worker_id,
              client_ip);

    while (1) {
      my_worker.status = 0; // IDLE
      enqueue_worker(&my_worker);

      pthread_mutex_lock(&my_worker.lock);
      while (my_worker.assigned_task == NULL) {
        pthread_cond_wait(&my_worker.cond, &my_worker.lock);
      }
      Task *task = my_worker.assigned_task;
      my_worker.assigned_task = NULL;
      my_worker.status = 1; // WORKING
      my_worker.start_time = time(NULL);
      pthread_mutex_unlock(&my_worker.lock);

      log_event(LOG_INFO, "event=task_sending worker_id=%d task_size=%d",
                my_worker.worker_id, task->size);

      if (send(client_sock, &task->size, sizeof(int), 0) <= 0 ||
          send(client_sock, task->data, task->size, 0) <= 0) {
        log_event(LOG_ERROR, "event=task_send_failed worker_id=%d",
                  my_worker.worker_id);
        log_event(LOG_WARN,
                  "event=task_requeued task_size=%d reason=send_failure",
                  task->size);
        enqueue_task_front(task);
        break;
      }

      int recv_worker_id;
      int output_size;

      if (recv(client_sock, &recv_worker_id, sizeof(int), 0) <= 0 ||
          recv(client_sock, &output_size, sizeof(int), 0) <= 0) {
        log_event(LOG_ERROR, "event=result_recv_failed worker_id=%d",
                  my_worker.worker_id);
        log_event(LOG_WARN,
                  "event=task_requeued task_size=%d reason=recv_failure",
                  task->size);
        enqueue_task_front(task);
        break;
      }

      char *output = malloc(output_size + 1);
      output[output_size] = '\0';
      if (recv_all(client_sock, output, output_size) == -1) {
        log_event(LOG_ERROR,
                  "event=result_data_failed worker_id=%d output_size=%d",
                  recv_worker_id, output_size);
        log_event(LOG_WARN,
                  "event=task_requeued task_size=%d reason=recv_failure",
                  task->size);
        enqueue_task_front(task);
        free(output);
        break;
      }

      time_t end_time = time(NULL);
      long duration_sec = (long)(end_time - my_worker.start_time);
      log_event(
          LOG_INFO,
          "event=task_completed worker_id=%d output_size=%d duration_sec=%ld",
          recv_worker_id, output_size, duration_sec);

      send(task->client_sock, &recv_worker_id, sizeof(int), 0);
      send(task->client_sock, &output_size, sizeof(int), 0);
      send(task->client_sock, output, output_size, 0);

      log_event(LOG_INFO,
                "event=result_relayed worker_id=%d client_fd=%d output_size=%d",
                recv_worker_id, task->client_sock, output_size);

      close(task->client_sock);

      free(output);
      free(task->data);
      free(task);
    }

    log_event(LOG_WARN, "event=worker_disconnected worker_id=%d ip=%s",
              my_worker.worker_id, my_worker.client_ip);
    remove_worker_global(&my_worker);
    close(client_sock);
    pthread_mutex_destroy(&my_worker.lock);
    pthread_cond_destroy(&my_worker.cond);
  } else {
    close(client_sock);
  }
}
// end Vamsi
// begin supprit

typedef struct {
  int sock;
  struct sockaddr_in addr;
} ThreadArgs;

void *thread_func(void *arg) {
  ThreadArgs *args = (ThreadArgs *)arg;
  int client_sock = args->sock;
  struct sockaddr_in client_addr = args->addr;
  free(args);

  handle_connection(client_sock, client_addr);
  return NULL;
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  logger_init("SERVER", "server.log", 1);

  // Setup the TUI with fixed header and scrolling region
  setup_ui(8);



  int server_fd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }

  int port = PORT;
  char *env_port = getenv("PORT");
  if (env_port) {
    port = atoi(env_port);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    // Election tie: another worker won and grabbed the port. Revert to worker.
    execl("./worker", "./worker", NULL);
    perror("Bind failed & reverting to worker also failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 10) < 0) {
    perror("Listen failed");
    exit(EXIT_FAILURE);
  }

  init_queues();

  pthread_t disp_tid, broad_tid, dash_tid;
  pthread_create(&disp_tid, NULL, dispatcher_thread, NULL);
  pthread_detach(disp_tid);

  pthread_create(&broad_tid, NULL, udp_broadcast_thread, NULL);
  pthread_detach(broad_tid);

  pthread_create(&dash_tid, NULL, dashboard_thread, NULL);
  pthread_detach(dash_tid);

  while (1) {
    addr_len = sizeof(client_addr);
    int sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (sock < 0) {
      perror("Accept failed");
      continue;
    }

    ThreadArgs *args = malloc(sizeof(ThreadArgs));
    args->sock = sock;
    args->addr = client_addr;

    pthread_t tid;
    if (pthread_create(&tid, NULL, thread_func, args) != 0) {
      perror("Thread creation failed");
      close(sock);
      free(args);
      continue;
    }
    pthread_detach(tid);
  }

  return 0;
}
// end supprit