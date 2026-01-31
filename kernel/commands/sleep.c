#include "programs.h"
#include "sched.h"
#include "lib.h"

/* Need access to shell_sigint? 
   The original code checked 'shell_sigint'. 
   Since this is now a separated program, we need a way to check for interruptions.
   Usually programs should check a task-local signal flag or similar. Sched has signals?
   Assuming 'shell_sigint' is global in shell.c and not exported.
   But wait, 'prog_sleep' is running in the shell's task context (builtin-ish)? 
   Or is it expected to run in standard way?
   Builtins in MYRAS run in the shell task usually unless backgrounded.
   If we spawn a process for it, we can just yield loop.
   Let's assume for now it's okay to just yield loop and rely on user Ctrl+C killing the task eventually if it was spawned, 
   or if it is builtin called by shell, shell handles sigint checks between calls?
   
   If prog_* are called directly by shell:
   The shell loop 'exec_command_argv' calls the function.
   If we loop inside the function, we block the shell.
   We need 'shell_sigint' visible if we want to break early.
   Better: expose 'int shell_check_interrupt(void)' or similar from shell.h?
   
   For now, I will omit the sigint check inside the loop or use a syscall if available.
   Actually, 'yield()' is fine. If the user hits Ctrl+C, the shell's interrupt handler sets a flag.
   The shell loop checks it. But we are inside the function.
   
   Let's add 'extern volatile int shell_sigint;' declaration.
   Wait, linking will fail if shell_sigint depends on shell.o being linked. It is.
*/

extern volatile int shell_sigint;

int prog_sleep(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    if (argc < 2) return 0;
    /* parse integer seconds */
    int sec = 0; for (char *p = argv[1]; *p; ++p) { if (*p >= '0' && *p <= '9') sec = sec*10 + (*p - '0'); }
    /* coarse-grained sleep by yielding */
    int ticks = sec * 50; /* arbitrary ticks per second */
    for (int i = 0; i < ticks; ++i) {
        if (shell_sigint) break;
        yield();
    }
    return shell_sigint ? -1 : 0;
}
