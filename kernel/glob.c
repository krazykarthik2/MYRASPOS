#include "glob.h"
#include "lib.h"

/* Simple glob matcher supporting '*', '?', and character classes [abc], [a-z], and negation [^...]
   Returns 1 if pattern matches string exactly, 0 otherwise. */

static int match_class(const char *p, char c, int *consumed) {
    int neg = 0;
    int ok = 0;
    const char *q = p;
    if (*q == '^' || *q == '!') { neg = 1; ++q; }
    while (*q && *q != ']') {
        if (q[1] == '-' && q[2] && q[2] != ']') {
            char lo = *q; char hi = q[2];
            if (lo > hi) { char t = lo; lo = hi; hi = t; }
            if ((unsigned char)c >= (unsigned char)lo && (unsigned char)c <= (unsigned char)hi) ok = 1;
            q += 3; continue;
        }
        if (*q == c) ok = 1;
        ++q;
    }
    *consumed = (int)(q - p) + 1; /* include closing ] */
    return neg ? !ok : ok;
}

int glob_match(const char *pattern, const char *s) {
    if (!pattern) return 0;
    const char *p = pattern;
    const char *str = s;
    while (*p) {
        if (*p == '*') {
            /* skip consecutive stars */
            while (*p == '*') ++p;
            if (!*p) return 1; /* trailing * matches rest */
            /* try to match the rest of pattern at each position */
            const char *t = str;
            while (*t) {
                if (glob_match(p, t)) return 1;
                ++t;
            }
            return 0;
        } else if (*p == '?') {
            if (!*str) return 0;
            ++p; ++str;
        } else if (*p == '[') {
            ++p;
            int consumed = 0;
            int ok = match_class(p, *str, &consumed);
            if (!ok) return 0;
            p += consumed;
            ++str;
        } else {
            if (*str == '\\' && p[0] == '\\' && p[1]) {
                /* escaped char in pattern handled literally (rare) */
                ++p; /* skip backslash */
            }
            if (*p != *str) return 0;
            ++p; ++str;
        }
    }
    return *str == '\0';
}
