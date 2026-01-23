#ifndef KERNEL_LIB_H
#define KERNEL_LIB_H

#include <stddef.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
int strncmp(const char *a, const char *b, size_t n);

char *strrchr(const char *s, int c);

/* minimal string helpers not provided by a libc in freestanding mode */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strcpy(char *dest, const char *src);
char *strtok(char *s, const char *delim);
int atoi(const char *s);
int levenshtein_distance(const char *s1, const char *s2);
int tolower(int c);
char *strcasestr(const char *haystack, const char *needle);
int levenshtein_distance_ci(const char *s1, const char *s2);

#endif
