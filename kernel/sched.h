#ifndef SCHED_H
#define SCHED_H

#include <stddef.h>
#include <stdint.h>

typedef void (*task_fn)(void *arg);

int scheduler_init(void);
int task_create(task_fn fn, void *arg);
void schedule(void);
void yield(void);
int task_kill(int id);
int task_exists(int id);
int task_current_id(void);
int task_set_fn_null(int id);
/* block current task until tick (monotonic) */
void task_block_current_until(uint32_t wake_tick);
/* advance scheduler tick (called by timer) and wake tasks */
void scheduler_tick_advance(uint32_t delta_ms);
/* get current scheduler tick (ms) */
uint32_t scheduler_get_tick(void);
/* collect task ids into out array, return count (max entries limited by 'max') */
int task_list(int *out, int max);
/* collect per-task stats: ids, run_counts, start_ticks. Returns number of tasks.
	If total_runs_out is non-NULL, sets it to total run count across all tasks. */
int task_stats(int *ids_out, int *run_counts_out, int *start_ticks_out, int max, int *total_runs_out);

#endif
