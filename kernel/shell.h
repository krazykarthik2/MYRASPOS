#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

/* Execute a command line through the shell's logic (parsing, globbing, pipelines) 
   and capture the final output in the provided buffer.
   Returns the number of bytes written to out, or -1 on error. */
int shell_exec(const char *cmdline, char *out, size_t out_cap);

#endif
