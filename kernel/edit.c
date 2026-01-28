#include "programs.h"
#include "editor_app.h"
#include <stddef.h>
#include <string.h>

int prog_edit(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    if (argc < 2) {
        editor_app_start(NULL);
    } else {
        editor_app_start(argv[1]);
    }
    return 0;
}
