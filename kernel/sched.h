#ifndef SCHED_H
#define SCHED_H

#include <stddef.h>

typedef void (*task_fn)(void *arg);

int scheduler_init(void);
int task_create(task_fn fn, void *arg);
void schedule(void);
void yield(void);

#endif
