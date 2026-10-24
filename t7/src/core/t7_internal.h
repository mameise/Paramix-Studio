/*
 * t7_internal.h - shared state structure for the emulator modules
 *
 * Not exposed to the Pebble app — only included by t7_*.c files.
 *
 * Performance note: hot-path state (CPU registers, work RAM, display RAM,
 * CPU register array, page tables) is exposed as separate global
 * variables rather than fields of a single g_sys struct. The reason is
 * that GCC treats `g_sys->foo` as a load through a pointer that could be
 * invalidated by any function call (because g_sys itself could in theory
 * be reassigned). With globals it can compute the address at compile time
 * and skip a level of indirection in the inner emulation loop. This
 * matters at ~750,000 memory accesses per emulated frame.
 */

#ifndef T7_INTERNAL_H
#define T7_INTERNAL_H

// ----- Hot-Path-Platzierung: heisser Emulator-Pfad ins IRAM (kein Flash-
//       Cache-Miss). Bei IRAM-Overflow im Linker -> #define T7_HOT leer.
#ifndef T7_HOT
#  if defined(ESP_PLATFORM)
#    include "esp_attr.h"
#    define T7_HOT IRAM_ATTR
#  else
#    define T7_HOT
#  endif
#endif


#include "t7.h"

// 6502 registers (split out as a global, not behind a pointer).
typedef struct {
  uint16_t pc;
  uint8_t  a;
  uint8_t  x;
  uint8_t  y;
  uint8_t  s;
  uint8_t  p;
} cpu6502_t;

// Small frequently-accessed state kept as globals (compile-time addresses).
extern cpu6502_t g_cpu;
extern uint8_t g_wram[T7_WRAM_SIZE];
extern uint8_t g_dram[T7_DRAM_SIZE];
extern uint8_t g_cpureg[T7_CPUREG_SIZE];

// Large buffers are heap-allocated to keep .bss under the Pebble's
// 16-bit virtual-size limit (65535 bytes). They're only ever touched
// through the page table after init, so a heap pointer costs the same
// as a global array: page_table[p] already holds the absolute address
// either way.
extern uint8_t *g_rom_bank_buf;
extern uint8_t *g_static_rom;

// IRQ vector table (16 entries, loaded once at init).
extern uint16_t g_irq_vectors[16];

// Set non-zero by t7_fire_irq whenever it sets a bit in cpureg[$73:$74].
// Cleared by t7_cpu_irq when it services the highest-priority pending
// IRQ. The CPU step checks this single byte instead of reloading both
// cpureg bytes on every instruction — at ~3 million steps/sec that saves
// ~6 million memory loads/sec.
extern uint8_t g_irq_pending_any;
/* 1.6.9 (test12) battery, as TamaGo-Pebble v0.3.0: T7_IDLE jumps over the passes of the main loop's wait at $CE66
 * (bit-identical), T7_STOP halts the CPU when the firmware writes 7 to $3001 (until an IRQ or a key) */
#define T7_IDLE_PC 0xCE66u
extern uint8_t g_t7_idle_ok, g_t7_halted;
extern uint32_t g_t7_idle_cyc, g_t7_keyact;
void t7_idle_check(void);

// Misc runtime state.
extern uint8_t  g_rom_bank_id;
extern bool     g_rom_loaded;
extern uint8_t  g_keys;
extern ResHandle g_rom_resource;

// Whether the emulator is initialised. Used by t7_release to know
// whether g_dram etc were ever populated.
extern bool g_initialised;

// ----- Memory access (fast inline path) -----------------------------------
//
// Page table: 256 entries, one per 256-byte page of the 64K address space.
// Each entry points at the backing RAM/ROM buffer for that page, or NULL
// if the page needs slow-path I/O handling.
extern uint8_t *t7_read_page[256];
extern uint8_t *t7_write_page[256];

uint8_t t7_io_read(uint16_t addr);
void    t7_io_write(uint16_t addr, uint8_t value);

// ----- Profiling counters (referenced by the inline read/write below) -----
//
// T7_PROFILE is set in the public t7.h. When OFF (the production
// default), PROFILE_INC compiles to nothing and the counter struct
// isn't even instantiated.

#if T7_PROFILE
typedef struct {
  uint32_t opcodes;        // t7_cpu_step calls
  uint32_t reads_fast;     // hit page-table (WRAM/DRAM/ROM)
  uint32_t reads_io;       // missed → t7_io_read
  uint32_t writes_fast;    // hit page-table
  uint32_t writes_io;      // hit I/O area
  uint32_t writes_dropped; // hit ROM (silently ignored)
  uint32_t irqs;           // t7_fire_irq calls
  uint32_t nmis;           // t7_fire_nmi calls
  uint32_t irq_entries;    // actual CPU IRQ entries (after mask check)
  uint32_t nmi_entries;    // actual CPU NMI entries
} t7_profile_t;

extern t7_profile_t g_t7_profile;
#  define PROFILE_INC(field)  (g_t7_profile.field++)
#else
#  define PROFILE_INC(field)  ((void)0)
#endif

#define T7_INLINE static inline __attribute__((always_inline))

T7_INLINE uint8_t t7_read(uint16_t addr)
{
  uint8_t *page = t7_read_page[addr >> 8];
  if (page) { PROFILE_INC(reads_fast); return page[addr & 0xFF]; }
  PROFILE_INC(reads_io);
  return t7_io_read(addr);
}

T7_INLINE uint16_t t7_read16(uint16_t addr)
{
  uint8_t lo = t7_read(addr);
  uint8_t hi = t7_read((addr + 1) & 0xFFFF);
  return lo | (hi << 8);
}

T7_INLINE void t7_write(uint16_t addr, uint8_t value)
{
  uint8_t *page = t7_write_page[addr >> 8];
  if (page) { PROFILE_INC(writes_fast); page[addr & 0xFF] = value; return; }
  if ((addr >> 12) == 0x3) { PROFILE_INC(writes_io); t7_io_write(addr, value); return; }
  PROFILE_INC(writes_dropped);
}

// ROM banking
extern uint32_t g_t7_static_off;   /* 0 Tama-Go, 0x4000 Friends */
extern uint8_t  g_t7_banks;
void t7_set_rom_bank(uint8_t bank);
bool t7_load_static_rom(void);

// CPU step (defined in t7_cpu.c)
uint8_t t7_cpu_step(void);
void    t7_cpu_reset(void);
void    t7_cpu_nmi(void);
void    t7_cpu_irq(void);

// IRQ logic (defined in t7_memory.c since it touches cpureg)
void t7_fire_irq(uint8_t i);
void t7_fire_nmi(uint8_t i);

#endif // T7_INTERNAL_H
