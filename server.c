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
