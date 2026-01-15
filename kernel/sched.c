#include "sched.h"
#include "kmalloc.h"
#include "uart.h"
#include <stdint.h>

struct task {
    int id;
    task_fn fn;
    void *arg;
    struct task *next;
};

static struct task *task_head = NULL;
static struct task *task_cur = NULL;
static int next_task_id = 1;

int scheduler_init(void) {
    task_head = NULL;
    task_cur = NULL;
    next_task_id = 1;
    return 0;
}

int task_create(task_fn fn, void *arg) {
    struct task *t = kmalloc(sizeof(*t));
    if (!t) return -1;
    t->id = next_task_id++;
    t->fn = fn;
    t->arg = arg;
    if (!task_head) {
        task_head = t;
        t->next = t;
    } else {
        t->next = task_head->next;
        task_head->next = t;
    }
    return t->id;
}

void schedule(void) {
    if (!task_head) return;
    if (!task_cur) task_cur = task_head;
    else task_cur = task_cur->next;
    // run task function cooperatively
    if (task_cur && task_cur->fn) {
        task_cur->fn(task_cur->arg);
    }
}

void yield(void) {
    schedule();
}
