#pragma once
#include <stddef.h>
typedef struct FILE FILE;
int printf(const char *f, ...);
int snprintf(char *b, size_t n, const char *f, ...);
