/* PokeMiniEmu: a small memory stream for the core's state (replaces libretro-common's) */
#ifndef PM_MEMSTREAM_H
#define PM_MEMSTREAM_H
#include <stdint.h>
#include <stddef.h>
typedef struct memstream { uint8_t *buf; uint32_t size, pos; } memstream_t;
uint64_t memstream_read(memstream_t *s, void *d, uint64_t n);
uint64_t memstream_write(memstream_t *s, const void *d, uint64_t n);
int64_t memstream_seek(memstream_t *s, int64_t off, int whence);
uint64_t memstream_pos(memstream_t *s);
#endif
