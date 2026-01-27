#include "sched.h"
#include "kmalloc.h"
#include "uart.h"
#include <stdint.h>
#include "lib.h"
#include "lib.h"
#include "timer.h"
#include "irq.h"
#include "palloc.h"

extern void cpu_switch_to(struct task_context *prev, struct task_context *next);
extern void ret_from_fork(void);
extern void task_exit(int code); 

/* Forward declarations for zombie management */
static void kill_children_of(int parent_id);
int task_kill(int id);

enum block_reason {
    BLOCK_NONE = 0,
    BLOCK_TIMER,
    BLOCK_EVENT
};

struct task {
    int id;
    task_fn fn;
    void *arg;
    int run_count;
    int start_tick;
    uint32_t wake_tick;      /* when to wake (monotonic ms) */
    enum block_reason block_type;
    task_fn saved_fn;        /* saved function when blocked */
    char name[16];
    int is_running;
    int zombie;              /* marked for cleanup after context switch */
    void *tty;
    
    void *stack; /* allocated kernel stack page */
    size_t stack_total_bytes;  /* total allocation including guard */
    struct task_context context;
    
    int parent_id; /* ID of parent task */
    struct task *next;
    uint32_t magic; /* 0xDEADC0DE */
};

static struct task *task_head = NULL;
static struct task *task_cur = NULL;
static struct task boot_task; /* Placeholder for boot execution context */
static int next_task_id = 1;
static int scheduler_tick = 0;
static int total_run_counts = 0;
static volatile int preempt_requested = 0;

/* Event waiting system */
struct event_waiter {
    int task_id;
    void *event_id;
    struct event_waiter *next;
};
static struct event_waiter *wait_list = NULL;

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

int task_create_with_stack(task_fn fn, void *arg, const char *name, size_t stack_kb) {
    struct task *t = kmalloc(sizeof(*t));
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    
    /* Stack: guard page (4KB) + usable stack (configurable) */
    #define STACK_GUARD_SIZE 4096
    size_t stack_size = stack_kb * 1024;
    size_t total_alloc = STACK_GUARD_SIZE + stack_size;
    
    t->stack = kmalloc(total_alloc);
    if (!t->stack) { kfree(t); return -1; }
    t->stack_total_bytes = total_alloc;
    memset(t->stack, 0, total_alloc);
    t->magic = 0xDEADC0DE;
    
    /* Fill guard page with poison pattern to detect underflow */
    uint32_t *guard = (uint32_t *)t->stack;
    for (size_t i = 0; i < STACK_GUARD_SIZE / 4; i++) {
        guard[i] = 0xDEADDEAD;
    }
    
    t->id = next_task_id++;
    t->fn = fn;
    t->arg = arg;
    t->run_count = 0;
    t->start_tick = scheduler_tick;
    t->is_running = 0;
    t->tty = NULL;
    t->parent_id = task_current_id();
    strncpy(t->name, name, 15); t->name[15] = '\0';
    
    /* Setup context for ret_from_fork */
    t->context.x19 = (uint64_t)fn;
    t->context.x20 = (uint64_t)arg;
    t->context.x30 = (uint64_t)ret_from_fork;
    
    /* Ensure SP is 16-byte aligned for ARM64 ABI */
    uint64_t stack_top = (uint64_t)t->stack + total_alloc;
    t->context.sp = stack_top & ~0xFULL;
    
    /* Stack canary at BOTTOM of stack (above guard page) to detect real overflow */
    #define STACK_GUARD_SIZE 4096
    *(uint64_t *)((uint64_t)t->stack + STACK_GUARD_SIZE) = 0xDEADBEEFCAFEBABE;

    /* DEBUG Trace Task Entry - DISABLED for performance */
    /*
    uart_puts("[sched] DBG: "); uart_puts(name);
    uart_puts(" fn="); uart_put_hex((uint32_t)(uintptr_t)fn);
    uart_puts(" ctx.x19="); uart_put_hex((uint32_t)t->context.x19);
    uart_puts(" ctx.x30="); uart_put_hex((uint32_t)t->context.x30);
    uart_puts(" total_alloc="); uart_put_hex(total_alloc);
    uart_puts("\n");
    */
    
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
        int count = 0;
        do {
            if (!v || ((uintptr_t)v & 7) != 0 || count > 100) {
                uart_puts("[sched] LIST CORRUPT after insert!\n");
                while(1);
            }
            count++;
            v = v->next;
        } while (v != task_head);
    }
    
    if (t->context.x19 == 0) {
        uart_puts("[sched] FATAL: x19 is 0 in task_create! fn=");
        uart_put_hex((uint32_t)(uintptr_t)fn);
        uart_puts("\n");
        while(1);
    }

    /*
    uart_puts("[sched] created task "); uart_puts(t->name); 
    uart_puts(" id="); uart_put_hex(t->id);
    uart_puts(" addr="); uart_put_hex((uint32_t)(uintptr_t)t);
    uart_puts(" stack_kb="); uart_put_hex(stack_kb);
    uart_puts(" parent="); uart_put_hex(task_current_id()); uart_puts("\n");
    */
    return t->id;
}

/* Default: 16KB stack for most tasks */
int task_create(task_fn fn, void *arg, const char *name) {
    return task_create_with_stack(fn, arg, name, 16);
}

/* Reap zombie tasks safely. Called at start of schedule() AFTER we've context-switched
 * away from the exiting task, so it's safe to free their memory now. */
static void reap_zombies(void) {
    if (!task_head) return;
    
    int found = 1;
    while (found) {
        found = 0;
        struct task *t = task_head;
        struct task *prev_t = NULL;
        
        /* Find the tail to help with head removal */
        struct task *tail = task_head;
        while (tail->next != task_head) {
            tail = tail->next;
        }
        
        struct task *start = task_head;
        do {
            if (t->zombie) {
                /* CRITICAL: Never reap the currently running task! 
                 * We are still using its stack. It will be reaped next schedule. */
                if (t == task_cur) {
                    prev_t = t;
                    t = t->next;
                    continue;
                }
                
                // uart_puts("[sched] reaping zombie "); uart_puts(t->name);
                // uart_puts(" id="); uart_put_hex(t->id); uart_puts("\n");
                
                /* Ensure children are also marked for death */
                kill_children_of(t->id);
                
                /* Remove from circular list */
                if (t->next == t) {
                    task_head = NULL;
                } else {
                    if (t == task_head) {
                        task_head = t->next;
                        tail->next = task_head;
                    } else {
                        prev_t->next = t->next;
                    }
                }
                
                struct task *to_free = t;
                /*
                uart_puts("[sched] freeing task struct and stack at addr=");
                uart_put_hex((uint32_t)(uintptr_t)to_free);
                uart_puts("\n");
                */
                
                /* Capture ID before freeing structure */
                int zombie_id = t->id;
                
                if (to_free->stack) kfree(to_free->stack);
                kfree(to_free);
                
                found = 1;

                /* Also remove from wait_list if present */
                struct event_waiter **wp = &wait_list;
                while (*wp) {
                    if ((*wp)->task_id == zombie_id) {
                        struct event_waiter *tmp = *wp;
                        *wp = (*wp)->next;
                        kfree(tmp);
                    } else {
                        wp = &(*wp)->next;
                    }
                }

                break; /* Restart scan since list changed */
            }
            prev_t = t;
            t = t->next;
        } while (task_head && t != start);
    }
}

void schedule(void) {
    /* FIRST: Reap any zombie tasks from previous exits. Safe because we're now
     * running on OS/boot stack or a different task's stack. */
    reap_zombies();
    
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
    
    /* CRITICAL: Disable IRQs during task selection and switch to prevent reentrancy */
    unsigned long sched_flags = irq_save();

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
    int runnable_count = 0;
    struct task *last_runnable = NULL;

    /* Count runnable tasks in one pass if possible, or just find the next one */
    struct task *v = task_head;
    do {
        if (v->fn != NULL) {
            runnable_count++;
            last_runnable = v;
        }
        v = v->next;
    } while (v != task_head);

    if (runnable_count == 0) {
        irq_restore(sched_flags);
        return;
    }
    
    /* Optimization: B. Scheduler optimizations - Avoid context switch when only one runnable task */
    if (runnable_count == 1) {
        if (prev == last_runnable) return;
        next = last_runnable;
    } else {
        while (next && (next->fn == NULL) && attempts < 1000) {
            next = next->next;
            attempts++;
            if (next == start) break; /* wrapped around */
        }
    }

    if (!next || next->fn == NULL) return;

    /* Don't switch if target is same as current */
    if (next == prev) return;

    /* Perform Switch */
    if (next->magic != 0xDEADC0DE) {
        uart_puts("[sched] CRITICAL: task magic corrupted for "); uart_puts(next->name);
        uart_puts(" magic="); uart_put_hex(next->magic);
        uart_puts(" addr="); uart_put_hex((uint32_t)(uintptr_t)next);
        uart_puts("\n");
        panic("Task corruption detected");
    }
    task_cur = next;
    
    /*
    if (next->id == 7 && next->run_count == 1) {
        uart_puts("[sched] STARTING FILES EXPLORER TASK\n");
    }
    */

    prev->is_running = 0;
    next->is_running = 1;
    
    /* account run for next task */
    next->run_count++;
    total_run_counts++;

    if (next->context.x19 == 0 && next->id != 0) { // x19 might be 0 for boot task? No, boot task has running state.
         // Actually boot task context is undefined/garbage until we switch away from it. 
         // But for a NEW task, x19 must be fn.
         if (next->run_count == 1) { // First run
             uart_puts("[sched] PRE-SWITCH CHECK FAIL: x19 is 0 for new task!\n");
             uart_puts(" addr="); uart_put_hex((uint32_t)(uintptr_t)next);
             uart_puts("\n");
             while(1);
         }
    }
    
    // Check x30 (LR)
    if (next->context.x30 == 0 && next->id != 0 && next->run_count == 1) {
         uart_puts("[sched] PRE-SWITCH CHECK FAIL: x30 is 0 for new task!\n");
         while(1);
    }
    
    /* Debug context before switch (SILENT for performance) */
    /*
    uart_puts("[sched] switching. next="); uart_puts(next->name);
    uart_puts(" id="); uart_put_hex(next->id);
    uart_puts(" x30="); uart_put_hex((uint32_t)next->context.x30);
    uart_puts(" sp="); uart_put_hex((uint32_t)next->context.sp);
    uart_puts("\n");
    */

    if (next->context.x30 == 0) {
        uart_puts("[sched] ERROR: x30 is 0!\n");
        while(1);
    }
    
    /* Detect if x30 was corrupted with a task ID pattern */
    uint64_t lr = next->context.x30;
    /* Valid kernel code is in range 0x40800000 - 0x41000000 */
    if (lr < 0x40800000ULL || lr > 0x41000000ULL) {
        uart_puts("[sched] ERROR: x30 out of kernel range! x30=");
        uart_put_hex((uint32_t)(lr >> 32));
        uart_put_hex((uint32_t)lr);
        uart_puts(" task="); uart_puts(next->name);
        uart_puts(" id="); uart_put_hex(next->id);
        uart_puts("\n");
        while(1);
    }
    
    /* Additional validation for critical addresses (SILENT for performance) */
    /*
    if (next->id == 1) { 
        uart_puts("[sched] SWITCHING TO INIT - validating context\n");
        uart_puts("  x30="); uart_put_hex((uint32_t)next->context.x30);
        uart_puts(" sp="); uart_put_hex((uint32_t)next->context.sp);
        uart_puts(" x19="); uart_put_hex((uint32_t)next->context.x19);
        uart_puts("\n");
    }
    */
    
    /* CRITICAL: Validate stack integrity before switch */
    if (next->stack) {
        #define STACK_GUARD_SIZE 4096
        size_t total_alloc = next->stack_total_bytes;
        
        /* Check guard page for underflow */
        uint32_t *guard = (uint32_t *)next->stack;
        int guard_corrupted = 0;
        int corrupt_offset = 0;
        for (int i = 0; i < STACK_GUARD_SIZE / 4; i++) {
            if (guard[i] != 0xDEADDEAD) {
                guard_corrupted = 1;
                corrupt_offset = i * 4;
                break;
            }
        }
        
        if (guard_corrupted) {
            uart_puts("[sched] STACK UNDERFLOW! Task="); uart_puts(next->name);
            uart_puts(" guard corrupted at offset="); uart_put_hex(corrupt_offset);
            uart_puts("\n");
            while(1);
        }
        
        /* Check canary for overflow (at the bottom of usable stack) */
        uint64_t *canary_addr = (uint64_t *)((uint64_t)next->stack + STACK_GUARD_SIZE);
        uint64_t canary = *canary_addr;
        if (canary != 0xDEADBEEFCAFEBABE) {
            uart_puts("[sched] STACK OVERFLOW! (Canary Corrupted) Task="); uart_puts(next->name);
            uart_puts(" canary="); uart_put_hex((uint32_t)(canary >> 32));
            uart_put_hex((uint32_t)canary);
            uart_puts("\n");
            while(1);
        }
        
        /* Check if current SP is within valid range */
        uint64_t stack_bottom = (uint64_t)next->stack + STACK_GUARD_SIZE;
        uint64_t stack_top = (uint64_t)next->stack + total_alloc;
        if (next->context.sp < stack_bottom || next->context.sp > stack_top) {
            uart_puts("[sched] SP OUT OF BOUNDS! Task="); uart_puts(next->name);
            uart_puts(" sp="); uart_put_hex((uint32_t)next->context.sp);
            uart_puts(" valid_range="); uart_put_hex((uint32_t)stack_bottom);
            uart_puts("-"); uart_put_hex((uint32_t)stack_top);
            uart_puts("\n");
            while(1);
        }
    }

    /* Mask IRQs to ensure atomic switch (crucial for stack safety) */
    // IRQs already disabled via sched_flags
    
    cpu_switch_to(&prev->context, &next->context);
    
    /* Restore IRQs after returning from context switch */
    irq_restore(sched_flags);
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
    struct task *start = task_head;
    
    /* Marking children as zombies is fast and safe because no memory is freed yet */
    do {
        if (t->parent_id == parent_id && t->id != parent_id && !t->zombie) {
            uart_puts("[sched] Reaping child id="); uart_put_hex(t->id); 
            uart_puts(" ("); uart_puts(t->name); uart_puts(") due to parent exit\n");
            t->zombie = 1;
            t->fn = NULL;
            /* Recurse to kill grandchildren */
            kill_children_of(t->id);
        }
        t = t->next;
    } while (t != start);
}

int task_kill(int id) {
    if (id <= 0) return -1;
    
    /* Find the task */
    struct task *t = task_head;
    if (!t) return -1;
    
    struct task *found = NULL;
    struct task *start = task_head;
    do {
        if (t->id == id) {
            found = t;
            break;
        }
        t = t->next;
    } while (t != start);
    
    if (!found) return -1;
    if (found->zombie) return 0; /* Already being killed */
    
    /*
    uart_puts("[sched] marking task id="); uart_put_hex(id); 
    uart_puts(" ("); uart_puts(found->name); uart_puts(") for killing\n");
    */
    
    found->zombie = 1;
    found->fn = NULL; /* Stop scheduling it */
    
    /* Cascading kill children */
    kill_children_of(id);
    
    /* If we killed ourselves, switch away immediately */
    if (found == task_cur) {
        schedule();
    }
    
    return 0;
}

void task_exit(int code) {
    (void)code;
    /* Mark current task as zombie - do NOT free it while we're still running on its stack!
     * The scheduler will reap it safely after switching to another context. */
    if (task_cur && task_cur != &boot_task) {
        // uart_puts("[sched] task_exit: marking "); uart_puts(task_cur->name); uart_puts(" as zombie\n");
        task_cur->zombie = 1;
        task_cur->fn = NULL;  /* Make it non-runnable */
    }
    schedule(); /* Switch away - scheduler will reap us */
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
    task_cur->block_type = BLOCK_TIMER;
    schedule();
}

void task_wait_event(void *event_id) {
    if (!task_cur) return;
    
    struct event_waiter *w = kmalloc(sizeof(*w));
    if (!w) return;
    w->task_id = task_cur->id;
    w->event_id = event_id;
    
    unsigned long flags = irq_save();
    w->next = wait_list;
    wait_list = w;
    
    task_cur->saved_fn = task_cur->fn;
    task_cur->fn = NULL;
    task_cur->block_type = BLOCK_EVENT;
    irq_restore(flags);
    
    schedule();
}

void task_wake_event(void *event_id) {
    unsigned long flags = irq_save();
    struct event_waiter **prev = &wait_list;
    struct event_waiter *curr = wait_list;
    
    while (curr) {
        if (curr->event_id == event_id) {
            /* Wake the task */
            struct task *t = task_head;
            if (t) {
                do {
                    if (t->id == curr->task_id) {
                        if (t->fn == NULL && t->saved_fn != NULL) {
                            t->fn = t->saved_fn;
                            t->saved_fn = NULL;
                            t->block_type = BLOCK_NONE;
                        }
                        break;
                    }
                    t = t->next;
                } while (t != task_head);
            }
            
            /* Remove waiter */
            struct event_waiter *to_free = curr;
            *prev = curr->next;
            curr = curr->next;
            kfree(to_free);
        } else {
            prev = &curr->next;
            curr = curr->next;
        }
    }
    irq_restore(flags);
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
            uart_puts("\n");
            while(1);
        }

        if (t->fn == NULL && t->saved_fn != NULL && t->block_type == BLOCK_TIMER && (int)t->wake_tick <= scheduler_tick) {
            t->fn = t->saved_fn;
            t->saved_fn = NULL;
            t->block_type = BLOCK_NONE;
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

void scheduler_ret_from_fork_debug(void) {
    if (task_cur) {
        uart_puts("[sched] TASK_ENTRY: "); uart_puts(task_cur->name);
        uart_puts(" (id="); uart_put_hex(task_cur->id); uart_puts(")\n");
    }
}

void scheduler_switch_debug(uint64_t lr, uint64_t sp) {
    (void)sp;
    if (lr == 0) {
        uart_puts("LR is ZERO!\n");
    }
}
