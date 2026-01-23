#ifndef PROGRAMS_H
#define PROGRAMS_H

#include <stddef.h>



/* Forward declare program functions implemented in separate files */
int prog_echo(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_help(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_touch(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_write(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_cat(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_ls(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_rm(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_mkdir(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_rmdir(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_cp(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_mv(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_grep(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_head(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_tail(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_more(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
int prog_tree(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

typedef int (*prog_fn_t)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

int program_lookup(const char *name, prog_fn_t *out);
const char **program_list(size_t *count);

#endif
