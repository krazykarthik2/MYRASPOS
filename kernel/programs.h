#ifndef PROGRAMS_H
#define PROGRAMS_H

#include <stddef.h>

typedef int (*prog_fn_t)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

int program_lookup(const char *name, prog_fn_t *out);
const char **program_list(size_t *count);

#endif
