#include "sched.h"
#include "kmalloc.h"
#include "uart.h"
#include <stdint.h>

struct task {
    int id;
    task_fn fn;
    void *arg;
    int run_count;
    int start_tick;
    char name[16];
    struct task *next;
};

static struct task *task_head = NULL;
static struct task *task_cur = NULL;
static int next_task_id = 1;
static int scheduler_tick = 0;
static int total_run_counts = 0;

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
    t->run_count = 0;
    t->start_tick = scheduler_tick;
    /* default name: task<ID> */
    int nid = t->id; int p=0; char buf[16]; if (nid==0) { buf[p++]='0'; } else { int digs[8]; int di=0; while (nid>0 && di<8) { digs[di++]=nid%10; nid/=10; } for (int j=di-1;j>=0;--j) buf[p++]=(char)('0'+digs[j]); }
    buf[p]='\0'; strcpy(t->name, "task"); size_t nl=strlen(t->name); if (nl + p + 1 < sizeof(t->name)) memcpy(t->name+nl, buf, p+1);
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
        /* account run for this task */
        task_cur->run_count++;
        total_run_counts++;
        scheduler_tick++;
        task_cur->fn(task_cur->arg);
    }
}

void yield(void) {
    schedule();
}

int task_exists(int id) {
    if (!task_head) return 0;
    struct task *t = task_head;
    do {
        if (t->id == id) return 1;
        t = t->next;
    } while (t && t != task_head);
    return 0;
}

int task_kill(int id) {
    if (!task_head) return -1;
    struct task *prev = task_head;
    struct task *cur = task_head;
    /* find node */
    do {
        if (cur->id == id) break;
        prev = cur;
        cur = cur->next;
    } while (cur != task_head);
    if (cur->id != id) return -1;
    /* remove cur from list */
    if (cur == prev && cur->next == cur) {
        /* only node */
        task_head = NULL;
    } else {
        /* find previous properly: prev currently points to previous when loop ended */
        if (cur == task_head) {
            /* find tail */
            struct task *tail = task_head;
            while (tail->next != task_head) tail = tail->next;
            task_head = cur->next;
            tail->next = task_head;
        } else {
            prev->next = cur->next;
        }
    }
    /* if task_cur pointed to this task, advance it */
    if (task_cur == cur) task_cur = cur->next == cur ? NULL : cur->next;
    kfree(cur);
    return 0;
}

int task_current_id(void) {
    if (!task_cur) return -1;
    return task_cur->id;
}

int task_set_fn_null(int id) {
    if (!task_head) return -1;
    struct task *t = task_head;
    do {
        if (t->id == id) { t->fn = NULL; return 0; }
        t = t->next;
    } while (t && t != task_head);
    return -1;
}

int task_stats(int *ids_out, int *run_counts_out, int *start_ticks_out, int max, int *total_runs_out) {
    if (!task_head || max <= 0) { if (total_runs_out) *total_runs_out = total_run_counts; return 0; }
    int n = 0;
    struct task *t = task_head;
    do {
        if (n < max) {
            if (ids_out) ids_out[n] = t->id;
            if (run_counts_out) run_counts_out[n] = t->run_count;
            if (start_ticks_out) start_ticks_out[n] = t->start_tick;
        }
        n++;
        t = t->next;
    } while (t && t != task_head);
    if (total_runs_out) *total_runs_out = total_run_counts;
    return n < max ? n : max;
}

int task_list(int *out, int max) {
    if (!task_head || max <= 0) return 0;
    int n = 0;
    struct task *t = task_head;
    do {
        if (n < max) out[n++] = t->id;
        t = t->next;
    } while (t && t != task_head);
    return n;
}
