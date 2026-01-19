#include "sched.h"
#include "kmalloc.h"
#include "uart.h"
#include <stdint.h>
#include "lib.h"
#include "lib.h"
#include "timer.h"
#include "irq.h"

extern void cpu_switch_to(struct task_context *prev, struct task_context *next);
extern void ret_from_fork(void);
extern void task_exit(int code); /* helper implementation below */

struct task {
    int id;
    task_fn fn;
    void *arg;
    int run_count;
    int start_tick;
    uint32_t wake_tick;      /* when to wake (monotonic ms) */
    task_fn saved_fn;        /* saved function when blocked */
    char name[16];
    int is_running;
    void *tty;
    
    void *stack; /* allocated kernel stack page */
    struct task_context context;
    
    int parent_id; /* ID of parent task */
    struct task *next;
};

static struct task *task_head = NULL;
static struct task *task_cur = NULL;
static struct task boot_task; /* Placeholder for boot execution context */
static int next_task_id = 1;
static int scheduler_tick = 0;
static int total_run_counts = 0;
static volatile int preempt_requested = 0;

void scheduler_request_preempt(void) {
    preempt_requested = 1;
}

int scheduler_init(void) {
    task_head = NULL;
    /* Initialize with boot task as current */
    memset(&boot_task, 0, sizeof(boot_task));
    boot_task.id = 0;
    strcpy(boot_task.name, "boot");
    boot_task.is_running = 1;
    task_cur = &boot_task;
    
    next_task_id = 1;
    return 0;
}

int task_create(task_fn fn, void *arg, const char *name) {
    struct task *t = kmalloc(sizeof(*t));
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    
    t->stack = kmalloc(4096);
    if (!t->stack) { kfree(t); return -1; }
    
    t->id = next_task_id++;
    t->fn = fn;
    t->arg = arg;
    t->run_count = 0;
    t->start_tick = scheduler_tick;
    t->is_running = 0;
    t->tty = NULL;
    
    /* Setup context for ret_from_fork */
    t->context.x19 = (uint64_t)fn;
    t->context.x20 = (uint64_t)arg;
    t->context.x30 = (uint64_t)ret_from_fork;
    /* Ensure SP is 16-byte aligned */
    uint64_t sp = (uint64_t)t->stack + 4096;
    sp &= ~0xF; 
    t->context.sp = sp;
    
    if (name) {
        strncpy(t->name, name, sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    } else {
        /* default name: task<ID> */
        int nid = t->id; int p=0; char buf[16]; if (nid==0) { buf[p++]='0'; } else { int digs[8]; int di=0; while (nid>0 && di<8) { digs[di++]=nid%10; nid/=10; } for (int j=di-1;j>=0;--j) buf[p++]=(char)('0'+digs[j]); }
        buf[p]='\0'; 
        strcpy(t->name, "task"); 
        size_t nl=strlen(t->name); 
        if (nl + p < sizeof(t->name)) memcpy(t->name+nl, buf, p+1);
    }
    
    /* Set parent ID */
    t->parent_id = task_current_id(); 
    /* Boot task (0) or no current task means parent is 0 (root) */
    if (t->parent_id < 0) t->parent_id = 0;

    if (!task_head) {
        task_head = t;
        t->next = t;
    } else {
        /* DEBUG: Validate list BEFORE insertion */
        if (!task_head->next || ((uintptr_t)task_head->next & 7) != 0) {
            uart_puts("[sched] LIST CORRUPT BEFORE insert! head_addr=");
            uart_put_hex((uint32_t)(uintptr_t)task_head);
            uart_puts(" head->next=");
            uart_put_hex((uint32_t)(uintptr_t)task_head->next);
            uart_puts(" head_id="); uart_put_hex(task_head->id);
            uart_puts("\n");
            while(1);
        }
        
        t->next = task_head->next;
        task_head->next = t;
    }
    
    /* DEBUG: Validate list after insertion */
    {
        struct task *v = task_head;
        struct task *last_valid = NULL;
        int count = 0;
        do {
            if (!v || ((uintptr_t)v & 7) != 0 || count > 100) {
                uart_puts("[sched] LIST CORRUPT after insert! v=");
                uart_put_hex((uint32_t)(uintptr_t)v);
                uart_puts(" count="); uart_put_hex(count);
                if (last_valid) {
                    uart_puts(" last_valid_id="); uart_put_hex(last_valid->id);
                    uart_puts(" last_valid_name="); uart_puts(last_valid->name);
                }
                uart_puts("\n");
                while(1);
            }
            last_valid = v;
            count++;
            v = v->next;
        } while (v != task_head);
    }
    
    uart_puts("[sched] created task "); uart_puts(t->name); 
    uart_puts(" id="); uart_put_hex(t->id);
    uart_puts(" addr="); uart_put_hex((uint32_t)(uintptr_t)t);
    uart_puts(" parent="); uart_put_hex(task_current_id()); uart_puts("\n");
    return t->id;
}

void schedule(void) {
    /* update timer and dispatch polled IRQs */
    timer_poll_and_advance();
    irq_poll_and_dispatch();

    /* DEBUG: Check if task_head got corrupted to boot_task */
    if (task_head == &boot_task) {
        uart_puts("[sched] CRITICAL: task_head is boot_task! Resetting to NULL\n");
        task_head = NULL;
    }

    // Debug: Dump task list frequently
    static int dump_tick = 0;
    if (dump_tick++ % 50 == 0) {
        // uart_puts("\n[sched] scan: "); ... (Disabled)
    }

    if (!task_head) return;

    struct task *prev = task_cur;
    struct task *next;

    /* Safety: if task_cur is NULL or not in list, treat as boot task */
    if (!prev || (prev != &boot_task && ((uintptr_t)prev & 7) != 0)) {
        prev = &boot_task;
        task_cur = &boot_task;
    }

    /* If we are currently the boot task, start with head */
    if (prev == &boot_task) {
        next = task_head;
    } else {
        next = prev->next;
    }

    /* skip blocked tasks (fn == NULL) */
    struct task *start = next;
    int attempts = 0;
    while (next && (next->fn == NULL) && attempts < 1000) {
        next = next->next;
        attempts++;
        if (next == start) break; /* wrapped around */
    }

    /* If no runable task found, and we are not boot task, just return (continue running current) */
    /* If we ARE boot task and no runnable task, we must loop/wait (but for now just return/busyloop in main) */
    if (!next || next->fn == NULL) return;

    /* Don't switch if target is same as current */
    if (next == prev) return;

    /* Perform Switch */
    task_cur = next;
    
    if (next->id > 1) { 
        // uart_puts("[sched] switch to: "); uart_puts(next->name); uart_puts("\n"); 
    }

    prev->is_running = 0;
    next->is_running = 1;
    
    /* account run for next task */
    next->run_count++;
    total_run_counts++;

    cpu_switch_to(&prev->context, &next->context);
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

/* Helper to kill children recursively */
static void kill_children_of(int parent_id) {
    if (!task_head) return;
    struct task *t = task_head;
    /* This is tricky with a circular list while modifying it. 
       Safest approach: scan list, if match found, kill it and restart scan.
       Because kill modifies the list. */
    int found = 1;
    while (found) {
        found = 0;
        t = task_head;
        if (!t) break;
        do {
            if (t->parent_id == parent_id && t->id != parent_id) {
                 int child_id = t->id;
                 /* recursivley kill grandchildren first? task_kill will call us? 
                    No, avoiding infinite recursion nicely. */
                 uart_puts("[sched] cascading kill child id="); uart_put_hex(child_id); uart_puts("\n");
                 task_kill(child_id); /* This will recurse naturally */
                 found = 1; 
                 break; /* Restart scan since list changed */
            }
            t = t->next;
        } while (t && t != task_head);
    }
}

int task_kill(int id) {
    uart_puts("[sched] killing task id="); uart_put_hex(id); uart_puts("\n");
    
    /* First, kill all children to enforce hierarchy */
    kill_children_of(id);
    
    if (!task_head) return -1;
    
    /* Find the task to kill and its predecessor in one pass */
    struct task *prev = NULL;
    struct task *cur = task_head;
    struct task *found = NULL;
    
    /* Iterate through circular list */
    do {
        if (cur->id == id) {
            found = cur;
            break;
        }
        prev = cur;
        cur = cur->next;
    } while (cur != task_head);
    
    if (!found) return -1; /* Task not found */
    
    /* Special case: only one task in list */
    if (found->next == found) {
        task_head = NULL;
    } 
    /* Removing the head node */
    else if (found == task_head) {
        /* Find the tail (node whose next is head) */
        struct task *tail = task_head;
        while (tail->next != task_head) {
            tail = tail->next;
        }
        task_head = found->next;
        tail->next = task_head;
    }
    /* Removing a non-head node */
    else {
        /* prev was set in the search loop and points to the predecessor */
        prev->next = found->next;
    }
    
    /* Update task_cur if needed */
    if (task_cur == found) {
        /* If list is now empty, return to boot task, otherwise pick next valid */
        if (!task_head) {
            task_cur = &boot_task;
        } else {
            task_cur = task_head;
        }
    }
    
    /* DEBUG: Validate list after deletion (before free) */
    if (task_head) {
        struct task *v = task_head;
        int count = 0;
        do {
            if (!v || ((uintptr_t)v & 7) != 0 || count > 100) {
                uart_puts("[sched] LIST CORRUPT after kill! v=");
                uart_put_hex((uint32_t)(uintptr_t)v);
                uart_puts(" count="); uart_put_hex(count);
                uart_puts("\n");
                while(1);
            }
            count++;
            v = v->next;
        } while (v != task_head);
    }
    
    /* Free the task */
    uart_puts("[sched] freeing task at addr=");
    uart_put_hex((uint32_t)(uintptr_t)found);
    uart_puts("\n");
    if (found->stack) kfree(found->stack);
    kfree(found);
    return 0;
}

void task_exit(int code) {
    (void)code;
    task_kill(task_current_id());
    schedule(); /* should not return */
    for(;;) yield();
}

int task_current_id(void) {
    if (!task_cur) return -1;
    return task_cur->id;
}

void task_set_tty(int id, void *tty) {
    if (!task_head) return;
    struct task *t = task_head;
    do {
        if (t->id == id) { t->tty = tty; return; }
        t = t->next;
    } while (t && t != task_head);
}

void* task_get_tty(int id) {
    if (!task_head) return NULL;
    struct task *t = task_head;
    do {
        if (t->id == id) return t->tty;
        t = t->next;
    } while (t && t != task_head);
    return NULL;
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

int task_set_parent(int id, int parent_id) {
    if (!task_head) return -1;
    struct task *t = task_head;
    do {
        if (t->id == id) { 
            t->parent_id = parent_id; 
            return 0; 
        }
        t = t->next;
    } while (t && t != task_head);
    return -1;
}

void task_block_current_until(uint32_t wake_tick) {
    
    if (!task_cur) return;
    /* save current function pointer and mark blocked */
    task_cur->saved_fn = task_cur->fn;
    task_cur->fn = NULL;
    task_cur->wake_tick = wake_tick;
    schedule();
}

void scheduler_tick_advance(uint32_t delta_ms) {
    if (delta_ms == 0) return;
    scheduler_tick += (int)delta_ms;
    /* wake any tasks whose wake_tick <= scheduler_tick */
    if (!task_head) return;
    struct task *t = task_head;
    do {
        /* Safety check: ensure t is a valid pointer (8-byte aligned at least) */
        if (((uintptr_t)t & 7) != 0) {
            uart_puts("[sched] PANIC: Corrupted task list in tick! t=");
            uart_put_hex((uint32_t)(uintptr_t)t);
            uart_puts(" head=");
            uart_put_hex((uint32_t)(uintptr_t)task_head);
            uart_puts("\n");
            while(1);
        }

        if (t->fn == NULL && t->saved_fn != NULL && (int)t->wake_tick <= scheduler_tick) {
            t->fn = t->saved_fn;
            t->saved_fn = NULL;
        }
        t = t->next;
    } while (t && t != task_head);
}

uint32_t scheduler_get_tick(void) {
    return (uint32_t)scheduler_tick;
}

int task_stats(int *ids_out, int *run_counts_out, int *start_ticks_out, int *runnable_out, char *names_out, int max, int *total_runs_out) {
    if (!task_head || max <= 0) { if (total_runs_out) *total_runs_out = total_run_counts; return 0; }
    int n = 0;
    struct task *t = task_head;
    do {
        if (n < max) {
            if (ids_out) ids_out[n] = t->id;
            if (run_counts_out) run_counts_out[n] = t->run_count;
            if (start_ticks_out) start_ticks_out[n] = t->start_tick;
            if (runnable_out) runnable_out[n] = (t->fn != NULL);
            if (names_out) {
                strncpy(names_out + n * 16, t->name, 15);
                (names_out + n * 16)[15] = '\0';
            }
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
