#ifndef SCHED_H
#define SCHED_H

#include <stddef.h>
#include <stdint.h>

struct task_context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t x30;
    uint64_t sp;
};

typedef void (*task_fn)(void *arg);

int scheduler_init(void);
int task_create(task_fn fn, void *arg, const char *name);
void schedule(void);
void yield(void);
int task_kill(int id);
int task_exists(int id);
int task_current_id(void);
void task_set_tty(int id, void *tty);
void* task_get_tty(int id);
int task_set_fn_null(int id);
int task_set_parent(int id, int parent_id);  /* Change task's parent */
/* block current task until tick (monotonic) */
void task_block_current_until(uint32_t wake_tick);
/* advance scheduler tick (called by timer) and wake tasks */
void scheduler_tick_advance(uint32_t delta_ms);
/* get current scheduler tick (ms) */
uint32_t scheduler_get_tick(void);
/* request a preempt from IRQ handler */
void scheduler_request_preempt(void);
/* collect task ids into out array, return count (max entries limited by 'max') */
int task_list(int *out, int max);
/* collect per-task stats: ids, run_counts, start_ticks. Returns number of tasks.
	If total_runs_out is non-NULL, sets it to total run count across all tasks. */
int task_stats(int *ids_out, int *run_counts_out, int *start_ticks_out, int *runnable_out, char *names_out, int max, int *total_runs_out);

#endif
