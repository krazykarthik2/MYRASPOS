#include "lib.h"

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    size_t i = 0;
    for (; i < n && src[i]; ++i) d[i] = src[i];
    for (; i < n; ++i) d[i] = '\0';
    return dest;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca == '\0' && cb == '\0') return 0;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        ++a; ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (; *s; ++s) {
        if (*s == ch) return (char *)s;
    }
    return NULL;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { ++h; ++n; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char *strtok(char *s, const char *delim) {
    static char *saved = NULL;
    if (s != NULL) saved = s;
    if (saved == NULL) return NULL;

    /* skip leading delimiters */
    while (*saved && strchr(delim, (int)(unsigned char)*saved)) ++saved;
    if (!*saved) { saved = NULL; return NULL; }

    char *token = saved;
    /* find end of token */
    while (*saved && !strchr(delim, (int)(unsigned char)*saved)) ++saved;
    if (*saved) {
        *saved = '\0';
        ++saved;
    } else {
        saved = NULL;
    }
    return token;
}

int atoi(const char *s) {
    int sign = 1;
    int v = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '+') ++s;
    else if (*s == '-') { sign = -1; ++s; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return v * sign;
}
