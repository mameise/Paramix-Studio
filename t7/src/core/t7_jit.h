/* t7_jit.h - 6502 to Thumb-2 translator (T7_JIT), see t7_jit.c. Author: mameise */
#ifndef T7_JIT_H
#define T7_JIT_H
#include <stdint.h>
#include <stddef.h>
/* the 6502 state as the run loop keeps it (in and out of a block) */
typedef struct {
  uint32_t a, x, y, s, nzn, nzz, fc, fv, fi;
  uint32_t pc, cyc, io;          /* out: next pc, cycles used, io = cycles of the I/O write that ended the block (0: none) */
  int32_t snd;                   /* in: the sound countdown at the start */
  uint8_t *wram;
  uint8_t *const *rp, *const *wp;
  uint32_t tmp;
  int32_t rem;                   /* in: budget - cycles (blocks follow each other in the code while they fit) */
  uint32_t son;                  /* sound on: the blocks keep clear of the next sound event */
  void *tbl;
} T7J;
typedef struct { uint32_t key, code, pre, all, cnt; } T7JEnt;
extern uint32_t g_t7j_on, g_t7j_stat[4];
void t7_jit_init(void *buf, uint32_t bytes);
void t7_jit_flush(void);
T7JEnt *t7_jit_get(uint32_t key, uint32_t pc, uint8_t *const *rp, uint32_t b3);
void t7_jit_run(T7J *s, uint32_t code);
#endif
