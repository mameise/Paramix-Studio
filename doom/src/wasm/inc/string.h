#pragma once
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n); void *memset(void *d, int c, size_t n); void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n); int strncmp(const char *a, const char *b, size_t n); size_t strlen(const char *s);
char *strncpy(char *d, const char *s, size_t n);
