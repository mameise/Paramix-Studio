/* ARM7TDMI interpreter for T18 on the Paradise (instruction semantics from the T18Emu core). */
#ifndef T18CPU_H
#define T18CPU_H
#include <stdint.h>

/* Guest SRAM 0xF8000000, 128 KB (the TG18 window is 136 KB, but nothing above 128 KB is ever used:
 * reads 0). It is direct memory at t18_sram (on the Paradise the Paradise SRAM itself, host = guest +
 * 0x20000000), except for the hole [T18_HOLE_LO, T18_HOLE_HI): there the host memory holds the
 * emulator's own data (stack, state, tables). The guest's bytes in the hole are kept in pages of
 * 512 bytes that are made on the first write of a non-zero value (until then they read 0). All five
 * TG18 firmwares only zero this range at the start and use a few hundred bytes of it later; both
 * stacks lie above it. */
#define T18_SRAM_SIZE 0x20000u
#if defined(PARADISE) && !defined(T18_HOLE_LO)
#define T18_HOLE_LO 0xE600u
#define T18_HOLE_HI 0x14400u
#endif
#ifndef T18_HOLE_LO
#define T18_HOLE_LO 0u
#define T18_HOLE_HI 0u
#endif
#define T18_HOLE_PG 512u
#define T18_HOLE_N ((T18_HOLE_HI - T18_HOLE_LO) / T18_HOLE_PG)
static inline int t18_hole(uint32_t o) { return o - T18_HOLE_LO < T18_HOLE_HI - T18_HOLE_LO; }
/* a range [o, o + n) that touches the hole */
static inline int t18_hole_n(uint32_t o, uint32_t n) { return T18_HOLE_HI > T18_HOLE_LO && o < T18_HOLE_HI && o + n > T18_HOLE_LO; }

typedef struct arm7 arm7_t;
typedef uint32_t (*arm7_rd_fn)(void *ctx, uint32_t addr, int size);
typedef void (*arm7_wr_fn)(void *ctx, uint32_t addr, uint32_t val, int size);

struct arm7 {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t spsr;          /* current mode's spsr */
    uint32_t bank_usr[7];   /* r8..r14 for usr/sys */
    uint32_t bank_fiq[7];   /* r8..r14 fiq */
    uint32_t bank_irq[2], bank_svc[2], bank_abt[2], bank_und[2];
    uint32_t spsr_fiq, spsr_irq, spsr_svc, spsr_abt, spsr_und;
    uint64_t cycles;        /* master ticks */
    uint64_t idle_cycles;
    uint32_t cyc_mult;      /* master ticks per CPU cycle */
    int irq_line, fiq_line;
    int halted;
    uint32_t vector_base;
    void *ctx;
    arm7_rd_fn rd;
    arm7_wr_fn wr;
    /* store notify range (picture buffer), called before the store */
    uint32_t note_lo, note_hi;
    void (*note_fn)(void *ctx, uint32_t addr);
    uint32_t last_pc;
    int fault;
    int attn;               /* irq_line | fiq_line | halted, after every device access */
    uint32_t *jtab;         /* translator: return cache (pc, code) x 256 */
    uint32_t jsave;         /* translator: scratch word */
    uint32_t jsplo;         /* translator: host address of the lowest guest sp blocks may use (1.6.8) */
    /* idle loops: skip whole rounds of a loop that changes nothing */
    uint64_t idle_next;     /* next check (master ticks) */
    uint32_t idle_fail;
    int watch;              /* 1 while one round is watched */
    int (*io_stable)(void *ctx, uint32_t a);   /* reading a gives the same value until the next event */
    uint64_t idle_skipped;
    uint32_t idle_pc;       /* pc where a round last held still (0 = none yet) */
    uint64_t idle_win;      /* target of the run in which idle_pc was already tried */
};

extern uint8_t *t18_sram;              /* guest SRAM, 128 KB (the hole part is not the guest's) */
extern uint8_t *t18_hole_pg[T18_HOLE_N + 1];   /* pages of the hole, 0 = not made yet (reads 0) */
extern uint8_t *(*t18_hole_new)(void);  /* platform: a zeroed page of 512 bytes, 0 = none left */
extern int t18_oom;                     /* a write into the hole was lost (no page left) */
const uint8_t *t18_sram_rp(uint32_t o); /* guest SRAM offset -> byte, valid up to the end of its 512-byte page */
uint8_t *t18_sram_wp(uint32_t o, int make);   /* same for writing; 0 when it is a hole page not made (make: make it) */
uint32_t t18_sram_rd(uint32_t o, int size);
void t18_sram_wr(uint32_t o, uint32_t v, int size);   /* makes the hole page only for a non-zero value */
void t18_sram_clear(void);              /* all zero, hole pages given up (the platform takes them back) */
extern void (*t18_hole_free)(void);     /* platform: all hole pages are free again */
extern const uint8_t *t18_fblk[128];

void arm7_init(arm7_t *c);
void arm7_reset(arm7_t *c, uint32_t pc);
int64_t arm7_run(arm7_t *c, uint64_t target);
int arm7_step(arm7_t *c);
extern int t18_idle_on;
void arm7_exception(arm7_t *c, uint32_t mode, uint32_t vec, uint32_t ret);
uint32_t arm7_read32(arm7_t *c, uint32_t a);
uint16_t arm7_read16(arm7_t *c, uint32_t a);
uint8_t arm7_read8(arm7_t *c, uint32_t a);
void arm7_write32(arm7_t *c, uint32_t a, uint32_t v);
void arm7_write16(arm7_t *c, uint32_t a, uint32_t v);
void arm7_write8(arm7_t *c, uint32_t a, uint8_t v);
void arm7_set_mode(arm7_t *c, uint32_t mode);

#define ARM7_T (1u<<5)
#define ARM7_F (1u<<6)
#define ARM7_I (1u<<7)

typedef struct {
    uint32_t r[16], cpsr, spsr;
    uint32_t bank_usr[7], bank_fiq[7], bank_irq[2], bank_svc[2], bank_abt[2], bank_und[2];
    uint32_t spsr_fiq, spsr_irq, spsr_svc, spsr_abt, spsr_und;
    uint64_t cycles;
    int32_t halted, pad;
} arm7_state_t;
void arm7_save(arm7_t *c, arm7_state_t *s);
void arm7_load(arm7_t *c, const arm7_state_t *s);

#ifdef T18_JIT
int t18_jit_try(arm7_t *c, uint64_t target);
extern int t18_jit_off;
int t18_jit_code_line(uint32_t sram_off);
int t18_jit_code_range(uint32_t sram_off, uint32_t len);
void t18_jit_written(arm7_t *c, uint32_t a, uint32_t n);
#endif
#endif
