
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