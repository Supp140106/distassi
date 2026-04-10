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
  double cpu_usage;
  double memory_usage;
  Task *assigned_task;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  struct Worker *next;     // For idle queue
  struct Worker *all_next; // For global list
} Worker;

Worker *all_workers_head = NULL;
pthread_mutex_t all_workers_lock = PTHREAD_MUTEX_INITIALIZER;

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

void *dashboard_thread(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&all_workers_lock);

    // Clear screen and move to top-left without scrolling
    printf("\033[?25l\033[H\033[J");

    printf("==================================================================="
           "=====================\n");
    printf("  SERVER DASHBOARD (UDP Broadcasting on port %d)\n",
           UDP_DISCOVERY_PORT);
    printf("==================================================================="
           "=====================\n");
    printf("%-15s | %-10s | %-10s | %-10s | %-19s | %s\n", "Worker IP",
           "Worker ID", "CPU (%)", "RAM (%)", "Status", "Duration");
    printf("-------------------------------------------------------------------"
           "---------------------\n");

    Worker *curr = all_workers_head;
    int count = 0;
    while (curr) {
      char buf;
      int r = recv(curr->sock, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
      if (r == 0 || (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        curr->status = -1;
      }

      if (curr->status != -1) {
        count++;
        char status_str[64];
        char duration_str[64] = "-";
        if (curr->status == 0) {
          sprintf(status_str, "\033[32mIDLE\033[0m"); // Green
        } else {
          sprintf(status_str, "\033[33mWORKING\033[0m"); // Yellow
          time_t now = time(NULL);
          sprintf(duration_str, "%lds", now - curr->start_time);
        }
        printf("%-15s | %-10d | %-10.1f | %-10.1f | %-19s | %s\n",
               curr->client_ip, curr->worker_id, curr->cpu_usage,
               curr->memory_usage, status_str, duration_str);
      }
      curr = curr->all_next;
    }

    if (count == 0) {
      printf("\n            No workers connected yet.\n");
    }

    printf("==================================================================="
           "=====================\n");
    printf("Total Workers: %d\n", count);
    printf("Recent Events:\n");
    logger_lock();
    for (int i = 0; i < LOG_RING_SIZE; i++) {
      const char *line = logger_get_line(i);
      if (line[0] != '\0') {
        printf("  %s\n", line);
      }
    }
    logger_unlock();
    pthread_mutex_unlock(&all_workers_lock);
    sleep(1);
  }
  return NULL;
}

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
// end Ranjith
// begin sahanasri

void *dispatcher_thread(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&dispatch_lock);
    while (task_queue.front == NULL || idle_workers.front == NULL) {
      pthread_cond_wait(&dispatch_cond, &dispatch_lock);
    }

    // Dequeue oldest task (FIFO for tasks — fair to clients)
    Task *task = task_queue.front;
    task_queue.front = task_queue.front->next;
    if (task_queue.front == NULL)
      task_queue.rear = NULL;

    // --- Least-Loaded Worker Selection ---
    // Scan all idle workers and pick the one with the lowest load score.
    // Score = 0.7 * CPU% + 0.3 * RAM%  (lower is better)
    Worker *best = NULL;
    Worker *best_prev = NULL;
    double best_score = 1e18;

    Worker *prev = NULL;
    Worker *curr = idle_workers.front;
    while (curr) {
      double score = 0.7 * curr->cpu_usage + 0.3 * curr->memory_usage;
      if (score < best_score) {
        best_score = score;
        best = curr;
        best_prev = prev;
      }
      prev = curr;
      curr = curr->next;
    }

    // Remove the chosen worker from the idle queue
    if (best_prev == NULL) {
      // best is the front
      idle_workers.front = best->next;
    } else {
      best_prev->next = best->next;
    }
    if (idle_workers.rear == best) {
      idle_workers.rear = best_prev;
    }
    best->next = NULL;

    Worker *worker = best;

    pthread_mutex_lock(&worker->lock);
    worker->assigned_task = task;
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->lock);

    log_event(LOG_INFO,
              "event=task_dispatched worker_id=%d task_size=%d "
              "load_score=%.1f cpu=%.1f ram=%.1f",
              worker->worker_id, task->size, best_score, worker->cpu_usage,
              worker->memory_usage);

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
          curr->cpu_usage = cpu;
          curr->memory_usage = mem;
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
    my_worker.worker_id = worker_id; // Set ID immediately
    my_worker.status = 0;
    my_worker.cpu_usage = 0.0;
    my_worker.memory_usage = 0.0;
    my_worker.assigned_task = NULL;
    pthread_mutex_init(&my_worker.lock, NULL);
    pthread_cond_init(&my_worker.cond, NULL);

    add_worker_global(&my_worker);
    log_event(LOG_INFO, "event=worker_connected worker_id=%d ip=%s",
              worker_id, client_ip);

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
        log_event(LOG_WARN, "event=task_requeued task_size=%d reason=send_failure",
                  task->size);
        enqueue_task_front(task);
        break;
      }

      int worker_id;
      int output_size;

      if (recv(client_sock, &worker_id, sizeof(int), 0) <= 0 ||
          recv(client_sock, &output_size, sizeof(int), 0) <= 0) {
        log_event(LOG_ERROR, "event=result_recv_failed worker_id=%d",
                  my_worker.worker_id);
        log_event(LOG_WARN, "event=task_requeued task_size=%d reason=recv_failure",
                  task->size);
        enqueue_task_front(task);
        break;
      }

      my_worker.worker_id = worker_id; // Set ID here once received

      char *output = malloc(output_size);
      if (recv_all(client_sock, output, output_size) == -1) {
        log_event(LOG_ERROR,
                  "event=result_data_failed worker_id=%d output_size=%d",
                  worker_id, output_size);
        log_event(LOG_WARN, "event=task_requeued task_size=%d reason=recv_failure",
                  task->size);
        enqueue_task_front(task);
        free(output);
        break;
      }

      time_t end_time = time(NULL);
      long duration_sec = (long)(end_time - my_worker.start_time);
      log_event(LOG_INFO,
                "event=task_completed worker_id=%d output_size=%d duration_sec=%ld",
                worker_id, output_size, duration_sec);

      send(task->client_sock, &worker_id, sizeof(int), 0);
      send(task->client_sock, &output_size, sizeof(int), 0);
      send(task->client_sock, output, output_size, 0);

      log_event(LOG_INFO,
                "event=result_relayed worker_id=%d client_fd=%d output_size=%d",
                worker_id, task->client_sock, output_size);

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
  logger_init("SERVER", "server.log", 0);

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
    // Election tie: Another worker won the election a millisecond before us and
    // grabbed the port. Revert back to being a worker instead of shutting down.
    execl("./worker", "./worker", NULL);
    perror("Bind failed & reverting to worker failed");
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

  // Block prints since TUI takes over screen
  // printf("Server listening on port %d...\n", port);
  // printf("\n--- Connection Instructions ---\n");
  // ...

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