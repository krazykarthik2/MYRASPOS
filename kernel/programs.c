#include "programs.h"
#include <stddef.h>
#include "lib.h"

struct prog_entry { const char *name; prog_fn_t fn; };

static struct prog_entry prog_table[] = {
    {"echo", prog_echo},
    {"help", prog_help},
    {"touch", prog_touch},
    {"write", prog_write},
    {"cat", prog_cat},
    {"ls", prog_ls},
    {"rm", prog_rm},
    {"mkdir", prog_mkdir},
    {"rmdir", prog_rmdir},
    {"cp", prog_cp},
    {"mv", prog_mv},
    {"grep", prog_grep},
    {"head", prog_head},
    {"tail", prog_tail},
    {"more", prog_more},
    {"tree", prog_tree},
    {NULL, NULL}
};

int program_lookup(const char *name, prog_fn_t *out) {
    for (int i = 0; prog_table[i].name; ++i) {
        if (strcmp(name, prog_table[i].name) == 0) { *out = prog_table[i].fn; return 0; }
    }
    return -1;
}

const char **program_list(size_t *count) {
    static const char *names[16];
    size_t n = 0;
    for (int i = 0; prog_table[i].name && n < 16; ++i) names[n++] = prog_table[i].name;
    names[n] = NULL;
    if (count) *count = n;
    return names;
}
