#include "jfx/jfx_scheduler.h"
#include <pthread.h>
#include <stdlib.h>

#define MAX_TASKS 1024
#define MAX_WORKERS 16

typedef struct { void (*func)(void *); void *userdata; int priority; } task_t;
typedef struct {
    task_t tasks[MAX_TASKS];
    unsigned count, active, workers;
    pthread_t threads[MAX_WORKERS];
    pthread_mutex_t lock;
    pthread_cond_t ready, idle;
    bool running;
} scheduler_t;
static scheduler_t *g_scheduler;

static void *worker(void *arg) {
    scheduler_t *s = arg;
    for (;;) {
        pthread_mutex_lock(&s->lock);
        while (!s->count && s->running) pthread_cond_wait(&s->ready, &s->lock);
        if (!s->count && !s->running) { pthread_mutex_unlock(&s->lock); return NULL; }
        unsigned best = 0;
        for (unsigned i = 1; i < s->count; ++i)
            if (s->tasks[i].priority > s->tasks[best].priority) best = i;
        task_t task = s->tasks[best];
        for (unsigned i = best + 1; i < s->count; ++i) s->tasks[i - 1] = s->tasks[i];
        --s->count;
        ++s->active;
        pthread_mutex_unlock(&s->lock);
        task.func(task.userdata);
        pthread_mutex_lock(&s->lock);
        --s->active;
        pthread_cond_broadcast(&s->idle);
        pthread_mutex_unlock(&s->lock);
    }
}

bool scheduler_init(uint32_t workers) {
    if (g_scheduler) return false;
    if (!workers) workers = 1;
    if (workers > MAX_WORKERS) workers = MAX_WORKERS;
    scheduler_t *s = calloc(1, sizeof(*s));
    if (!s) return false;
    if (pthread_mutex_init(&s->lock, NULL)) { free(s); return false; }
    if (pthread_cond_init(&s->ready, NULL)) { pthread_mutex_destroy(&s->lock); free(s); return false; }
    if (pthread_cond_init(&s->idle, NULL)) { pthread_cond_destroy(&s->ready); pthread_mutex_destroy(&s->lock); free(s); return false; }
    s->running = true;
    for (; s->workers < workers; ++s->workers) {
        if (pthread_create(&s->threads[s->workers], NULL, worker, s)) {
            s->running = false;
            pthread_cond_broadcast(&s->ready);
            for (unsigned i = 0; i < s->workers; ++i) pthread_join(s->threads[i], NULL);
            pthread_cond_destroy(&s->idle); pthread_cond_destroy(&s->ready);
            pthread_mutex_destroy(&s->lock); free(s); return false;
        }
    }
    g_scheduler = s;
    return true;
}

void scheduler_shutdown(void) {
    scheduler_t *s = g_scheduler;
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    s->running = false;
    pthread_cond_broadcast(&s->ready);
    pthread_mutex_unlock(&s->lock);
    for (unsigned i = 0; i < s->workers; ++i) pthread_join(s->threads[i], NULL);
    g_scheduler = NULL;
    pthread_cond_destroy(&s->idle); pthread_cond_destroy(&s->ready);
    pthread_mutex_destroy(&s->lock); free(s);
}

bool scheduler_submit(void (*func)(void *), void *userdata, int priority) {
    scheduler_t *s = g_scheduler;
    if (!s || !func) return false;
    pthread_mutex_lock(&s->lock);
    bool ok = s->running && s->count < MAX_TASKS;
    if (ok) {
        s->tasks[s->count++] = (task_t){func, userdata, priority};
        pthread_cond_signal(&s->ready);
    }
    pthread_mutex_unlock(&s->lock);
    return ok;
}

uint32_t scheduler_pending_tasks(void) {
    scheduler_t *s = g_scheduler;
    if (!s) return 0;
    pthread_mutex_lock(&s->lock);
    uint32_t count = s->count;
    pthread_mutex_unlock(&s->lock);
    return count;
}

void scheduler_wait_idle(void) {
    scheduler_t *s = g_scheduler;
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    while (s->count || s->active) pthread_cond_wait(&s->idle, &s->lock);
    pthread_mutex_unlock(&s->lock);
}
