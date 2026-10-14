#ifndef STATE_H
#define STATE_H
#include "io.h"
#include "../core/tc.h"
typedef struct { u32 guest_off, guest_size, save_off, device; const volatile u8 *map; u32 spare_end; } StCfg;
int  st_flush_guest(Emu *e, const StCfg *c);
int  st_evict(Emu *e);
int  st_flush_step(Emu *e, const StCfg *c);   /* one part per call, 1 = done */
extern u32 st_n_step;
void st_init(const StCfg *c, u8 *buf4k);   /* buf4k: buffer for save/load */
void st_pages(Emu *e, const StCfg *c);      /* spare pages: load the table, set the pointers */
int  st_big_erase(Emu *e, uint32_t base);  /* hook tc_big_erase */
void st_drop(const StCfg *c);               /* throw the run state away */
extern u32 st_n_pages, st_n_free;
void st_save(Emu *e, const StCfg *c, u16 (*gram)[128]);
int  st_load(Emu *e, const StCfg *c, u16 (*gram)[128], u32 *pause_s);
#endif
