#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_TASKS 1024
#define MAX_WORKERS 16

typedef struct {
    void (*func)(void *);
    void *userdata;
    int priority;
} task_t;

typedef struct {
    task_t tasks[MAX_TASKS];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} task_queue_t;

typedef struct {
    task_queue_t queue;
    pthread_t threads[MAX_WORKERS];
    uint32_t worker_count;
    bool running;
} scheduler_t;

static scheduler_t *g_scheduler = NULL;

static void *worker_thread(void *arg) {
    scheduler_t *sched = (scheduler_t *)arg;
    
    while (sched->running) {
        pthread_mutex_lock(&sched->queue.lock);
        
        while (sched->queue.count == 0 && sched->running) {
            pthread_cond_wait(&sched->queue.not_empty, &sched->queue.lock);
        }
        
        if (!sched->running) {
            pthread_mutex_unlock(&sched->queue.lock);
            break;
        }
        
        task_t task = sched->queue.tasks[sched->queue.head];
        sched->queue.head = (sched->queue.head + 1) % MAX_TASKS;
        sched->queue.count--;
        
        pthread_cond_signal(&sched->queue.not_full);
        pthread_mutex_unlock(&sched->queue.lock);
        
        // Execute task
        if (task.func) {
            task.func(task.userdata);
        }
    }
    
    return NULL;
}

void scheduler_init(uint32_t worker_count) {
    if (g_scheduler) return;
    
    if (worker_count == 0) worker_count = 1;
    if (worker_count > MAX_WORKERS) worker_count = MAX_WORKERS;
    
    g_scheduler = calloc(1, sizeof(scheduler_t));
    g_scheduler->worker_count = worker_count;
    g_scheduler->running = true;
    
    pthread_mutex_init(&g_scheduler->queue.lock, NULL);
    pthread_cond_init(&g_scheduler->queue.not_empty, NULL);
    pthread_cond_init(&g_scheduler->queue.not_full, NULL);
    
    for (uint32_t i = 0; i < worker_count; i++) {
        pthread_create(&g_scheduler->threads[i], NULL, worker_thread, g_scheduler);
    }
    
    tilly_log_debug("scheduler", "Scheduler initialized with %u workers", worker_count);
}

void scheduler_shutdown(void) {
    if (!g_scheduler) return;
    
    g_scheduler->running = false;
    
    pthread_mutex_lock(&g_scheduler->queue.lock);
    pthread_cond_broadcast(&g_scheduler->queue.not_empty);
    pthread_mutex_unlock(&g_scheduler->queue.lock);
    
    for (uint32_t i = 0; i < g_scheduler->worker_count; i++) {
        pthread_join(g_scheduler->threads[i], NULL);
    }
    
    pthread_mutex_destroy(&g_scheduler->queue.lock);
    pthread_cond_destroy(&g_scheduler->queue.not_empty);
    pthread_cond_destroy(&g_scheduler->queue.not_full);
    
    free(g_scheduler);
    g_scheduler = NULL;
    
    tilly_log_debug("scheduler", "Scheduler shutdown");
}

bool scheduler_submit(void (*func)(void *), void *userdata, int priority) {
    if (!g_scheduler || !func) return false;
    
    pthread_mutex_lock(&g_scheduler->queue.lock);
    
    while (g_scheduler->queue.count == MAX_TASKS && g_scheduler->running) {
        pthread_cond_wait(&g_scheduler->queue.not_full, &g_scheduler->queue.lock);
    }
    
    if (!g_scheduler->running) {
        pthread_mutex_unlock(&g_scheduler->queue.lock);
        return false;
    }
    
    task_t *task = &g_scheduler->queue.tasks[g_scheduler->queue.tail];
    task->func = func;
    task->userdata = userdata;
    task->priority = priority;
    
    g_scheduler->queue.tail = (g_scheduler->queue.tail + 1) % MAX_TASKS;
    g_scheduler->queue.count++;
    
    pthread_cond_signal(&g_scheduler->queue.not_empty);
    pthread_mutex_unlock(&g_scheduler->queue.lock);
    
    return true;
}

uint32_t scheduler_pending_tasks(void) {
    if (!g_scheduler) return 0;
    pthread_mutex_lock(&g_scheduler->queue.lock);
    uint32_t count = g_scheduler->queue.count;
    pthread_mutex_unlock(&g_scheduler->queue.lock);
    return count;
}

void scheduler_wait_idle(void) {
    if (!g_scheduler) return;
    
    pthread_mutex_lock(&g_scheduler->queue.lock);
    while (g_scheduler->queue.count > 0) {
        pthread_cond_wait(&g_scheduler->queue.not_full, &g_scheduler->queue.lock);
    }
    pthread_mutex_unlock(&g_scheduler->queue.lock);
}