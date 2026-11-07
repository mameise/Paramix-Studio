/* PokeMiniEmu: the few C library parts the PokeMini core needs (no C library on the Paradise) */
#ifndef PM_STDLIB_H
#define PM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
static inline int abs(int x) { return x < 0 ? -x : x; }
#endif
