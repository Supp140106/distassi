
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include "common.h"
typedef struct Task {
    int size;
    char *data;
    int client_sock;
    struct Task *next;
} Task;

typedef struct Worker {
    int sock;
    Task *assigned_task;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct Worker *next;
} Worker;

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
void* dispatcher_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&dispatch_lock);
        while (task_queue.front == NULL || idle_workers.front == NULL) {
            pthread_cond_wait(&dispatch_cond, &dispatch_lock);
        }

        Task *task = task_queue.front;
        task_queue.front = task_queue.front->next;
        if (task_queue.front == NULL)
            task_queue.rear = NULL;

        Worker *worker = idle_workers.front;
        idle_workers.front = idle_workers.front->next;
        if (idle_workers.front == NULL)
            idle_workers.rear = NULL;

        pthread_mutex_lock(&worker->lock);
        worker->assigned_task = task;
        pthread_cond_signal(&worker->cond);
        pthread_mutex_unlock(&worker->lock);

        pthread_mutex_unlock(&dispatch_lock);
    }
    return NULL;
}
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
        printf("[%s] Task received and queued (size=%d)\n", client_ip, size);
    
    }

else if (type == REQUEST_TASK) {
        printf("[%s] Worker connected, registering as idle...\n", client_ip);

        Worker my_worker;
        my_worker.sock = client_sock;
        my_worker.assigned_task = NULL;
        pthread_mutex_init(&my_worker.lock, NULL);
        pthread_cond_init(&my_worker.cond, NULL);

        while (1) {
            enqueue_worker(&my_worker);

            pthread_mutex_lock(&my_worker.lock);
            while (my_worker.assigned_task == NULL) {
                pthread_cond_wait(&my_worker.cond, &my_worker.lock);
            }
            Task *task = my_worker.assigned_task;
            my_worker.assigned_task = NULL;
            pthread_mutex_unlock(&my_worker.lock);

            printf("[%s] Task assigned to worker, sending...\n", client_ip);

            if (send(client_sock, &task->size, sizeof(int), 0) <= 0 ||
                send(client_sock, task->data, task->size, 0) <= 0) {
                printf("[%s] Worker disconnected during task send. Retrying task...\n", client_ip);
                enqueue_task_front(task);
                break;
            }

            int worker_id;
            int output_size;

            if (recv(client_sock, &worker_id, sizeof(int), 0) <= 0 ||
                recv(client_sock, &output_size, sizeof(int), 0) <= 0) {
                printf("[%s] Worker disconnected before sending result. Retrying task...\n", client_ip);
                enqueue_task_front(task);
                break;
            }

            char *output = malloc(output_size);
            if (recv_all(client_sock, output, output_size) == -1) {
                printf("[%s] Worker disconnected during output transfer. Retrying task...\n", client_ip);
                enqueue_task_front(task);
                free(output);
                break;
            }

            printf("[%s] Result received from worker %d, forwarding to client...\n", client_ip, worker_id);

            send(task->client_sock, &worker_id, sizeof(int), 0);
            send(task->client_sock, &output_size, sizeof(int), 0);
            send(task->client_sock, output, output_size, 0);

            close(task->client_sock);

            free(output);
            free(task->data);
            free(task);
        }

        close(client_sock);
        pthread_mutex_destroy(&my_worker.lock);
        pthread_cond_destroy(&my_worker.cond);
    } else {
        close(client_sock);
    }
}
