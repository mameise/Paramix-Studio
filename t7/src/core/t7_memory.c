/*
 * t7_memory.c - memory map, ROM banking, and I/O registers
 *
 * Port of asterick/tamago. Highly optimized for the Pebble's ARM Cortex-M3.
 *
 * Memory map (16-bit address space):
 *   $0000-$0FFF   Work RAM     (1.5 KB, mirrored across 4 KB)
 *   $1000-$2FFF   Display RAM  (512 B, mirrored across 8 KB)
 *   $3000-$3FFF   I/O registers (256 B, mirrored 16x)
 *   $4000-$BFFF   Banked ROM   (32 KB, 20 banks selectable via $3000)
 *   $C000-$FFFF   Static ROM   (16 KB, always mapped from beginning of ROM)
 *
 * Performance: the inner emulation loop hits memory ~3-4 times per
 * instruction. To avoid expensive `addr % size` (modulo, which on M3
 * compiles to a software divide) and function-call overhead, we build a
 * page table at init: one pointer per 256-byte page of the 64K address
 * space, pointing at the backing RAM/ROM buffer. Reads and writes that
 * are pure RAM/ROM become a single array lookup + indirect load. Only
 * the 256 bytes of I/O space ($3000-$30FF) require the slow path with
 * register-specific handlers.
 */

#include "t7_internal.h"
#include "t7_eeprom.h"
#include "t7_sound.h"

// We need the resource ID generated from package.json's "media" entry.
// Defined in the generated header `src/resource_ids.auto.h` which is
// included via pebble.h.
#ifndef RESOURCE_ID_TAMA_ROM
#define RESOURCE_ID_TAMA_ROM 0
#endif

// ======================================================================
//  FIGUR / SPI TRACE  (Etappe 1 des Figuren-Reverse-Engineering)
//
//  Ziel: herausfinden, wie die Firmware mit einer gesteckten Gotchi-Figur
//  ueber SPI redet. Wir melden eine Figur als "gesteckt" (g_figure_id auf
//  PortA Bit 5+) und schneiden jeden Zugriff auf PortA ($3012) und die
//  SPI-Register ($30B0-$30BA) in einen Ringpuffer mit. Identische
//  aufeinanderfolgende Reads werden zusammengefasst (rep-Zaehler), damit
//  Status-Polling den Puffer nicht flutet.
//
//  Steuerung per Serial (siehe main.cpp): fig / spiarm / spidump / spiret
// ======================================================================
uint8_t g_figure_id = 0;     // 0 = keine Figur; 1..3 = Figur-Typ (live setzbar)
uint8_t g_spi_ret   = 0xFF;  // was $30B0-$30BA-Reads zurueckgeben (damit Status-Poll durchlaeuft)

#ifndef SPI_TRACE_MAX
#define SPI_TRACE_MAX 8192
#endif
typedef struct { uint8_t kind; uint8_t reg; uint8_t val; uint16_t rep; } spi_ev_t;
static spi_ev_t spi_trace[SPI_TRACE_MAX];
static int      spi_trace_n     = 0;
static bool     spi_trace_armed = false;  // Diagnose-Trace standardmaessig AUS (per 'spiarm' scharf)
static bool     ir_trace_armed  = false;  // IR-Trace ($3070-$307F) per 'irarm' scharf
// kind: 0=Reg-Read  1=Reg-Write  2=CSN-Wechsel(val=neuer Pegel)

static void spi_log(uint8_t kind, uint8_t reg, uint8_t val) {
  if (!spi_trace_armed && !ir_trace_armed) return;
  if (spi_trace_n > 0) {                       // gleiche Reads zusammenfassen
    spi_ev_t *e = &spi_trace[spi_trace_n - 1];
    if (e->kind == kind && e->reg == reg && e->val == val && e->rep < 0xFFFF) { e->rep++; return; }
  }
  if (spi_trace_n < SPI_TRACE_MAX) spi_trace[spi_trace_n++] = (spi_ev_t){ kind, reg, val, 1 };
  // voll -> einfrieren (nichts mehr aufzeichnen)
}

void    t7_figure_set(uint8_t id) { g_figure_id = id; }
uint8_t t7_figure_get(void)       { return g_figure_id; }
void    t7_spi_set_ret(uint8_t v) { g_spi_ret = v; }
void    t7_spitrace_arm(void)     { spi_trace_n = 0; spi_trace_armed = true; }
void    t7_irtrace_arm(void)      { spi_trace_n = 0; ir_trace_armed = true; spi_trace_armed = false; }
int     t7_spitrace_count(void)   { return spi_trace_n; }
void    t7_spitrace_get(int i, uint8_t *kind, uint8_t *reg, uint8_t *val, uint16_t *rep) {
  if (i < 0 || i >= spi_trace_n) { *kind=*reg=*val=0; *rep=0; return; }
  *kind = spi_trace[i].kind; *reg = spi_trace[i].reg;
  *val  = spi_trace[i].val;  *rep = spi_trace[i].rep;
}

// ======================================================================
//  SPI-FLASH-SLAVE (Gotchi-Figur)
//
//  Aus dem ROM-Disassembly ($EA6E / $EB20) rekonstruiertes Protokoll:
//   - CS = PortB-Data ($3016) Bit 4, aktiv-LOW.
//   - Kommando ueber $30B3 (P_SPI_Write): 0x03 = Flash-Read, dann 3 Adressbytes
//     (hi, mid, lo) = $71,$72,$73.
//   - Danach liefern $30B6-Reads (P_SPI_Read) flash[addr], flash[addr+1], ...
//   - $30B7 (P_SPI_Status) Bit 2 (0x04) = "Byte bereit".
//   - Presence-Check der Firmware: $3012 (PortA) Bit 5/6.
//  Verifiziert: Detektor liest Adresse 0x000001 und prueft auf 'AA 55' -
//  makiko.bin[1..2] = AA 55.
// ======================================================================
extern const unsigned char* figure_rom_ptr(void);
extern unsigned int         figure_rom_length(void);

// Echtes Full-Duplex-Modell: pro CS-low-Transaktion laeuft eine fortlaufende
// Transfer-Position. Bei einem Flash-Read (0x03) sind die ersten 4 Bytes
// Kommando+Adresse -> Flash gibt waehrenddessen NICHTS aus; die echten Daten
// kommen ab Position 4 (= flash[addr], flash[addr+1], ...). Die Firmware liest
// das exakt so: erst 4 "Leerbytes" wegwerfen, dann die Nutzdaten.
//   - RX wird beim SENDEN (Write $30B3) erzeugt und in eine FIFO gelegt.
//   - Reads ($30B6) holen die Bytes aus der FIFO (das waehrend des Sendens
//     gleichzeitig empfangene Byte - so funktioniert SPI).
// Ist kein 0x03 das erste Byte (z.B. Loopback-Selbsttest $CA2C), wird das
// gesendete Byte zurueckgespiegelt (Echo).
static uint8_t  spi_cs       = 1;       // 1=deasserted(high), 0=asserted(low)
static int      spi_pos      = 0;       // Transfer-Position seit CS-low
static uint8_t  spi_cmd0     = 0;       // erstes Byte = Kommando
static uint8_t  spi_addr_b[3];
static uint32_t spi_addr     = 0;
static bool     spi_is_read  = false;   // cmd0 == 0x03
static uint8_t  spi_rxfifo[256];
static uint8_t  spi_rxw = 0, spi_rxr = 0;
static const unsigned char* spi_rom = 0;   // pro Transaktion gecacht (spart Aufrufe pro Byte)
static unsigned int         spi_len = 0;

static void spi_cs_set(uint8_t level)   // level: Bit4-Pegel von $3016
{
  uint8_t cs = level ? 1 : 0;
  if (cs == spi_cs) return;
  spi_cs = cs;
  if (cs == 0) {                        // fallende Flanke -> neue Transaktion
    spi_pos = 0; spi_rxw = spi_rxr = 0; spi_is_read = false; spi_addr = 0;
    spi_len = figure_rom_length();      // aktive Figur einmalig cachen
    spi_rom = spi_len ? figure_rom_ptr() : 0;
  }
}

static void spi_tx(uint8_t v)           // Schreibzugriff auf $30B3 (= ein Transfer)
{
  if (spi_cs) return;                   // nur bei aktivem CS
  int p = spi_pos;
  uint8_t rx;
  if (p == 0) { spi_cmd0 = v; spi_is_read = (v == 0x03); }
  if (p >= 1 && p <= 3) spi_addr_b[p - 1] = v;
  if (p == 3 && spi_is_read)
    spi_addr = ((uint32_t)spi_addr_b[0] << 16) | ((uint32_t)spi_addr_b[1] << 8) | spi_addr_b[2];

  if (spi_is_read) {
    if (p < 4) rx = 0x00;               // waehrend cmd+addr: Flash noch stumm
    else if (spi_len) { uint32_t a = spi_addr + (uint32_t)(p - 4); rx = (a < spi_len) ? spi_rom[a] : 0x00; }
    else rx = 0xFF;                     // Lite (keine ROM) -> Signatur scheitert
  } else {
    rx = v;                             // Loopback/Echo
  }
  spi_rxfifo[spi_rxw++] = rx;           // 8-bit-Index wrappt automatisch (256)
  spi_pos++;
}

static uint8_t spi_rx(void)             // Lesezugriff auf $30B6
{
  if (spi_rxr != spi_rxw) return spi_rxfifo[spi_rxr++];
  return 0xFF;
}



// ----- Page table ---------------------------------------------------------
//
// 65536 / 256 = 256 pages. Each page's pointer either points at the start
// of a 256-byte RAM/ROM region OR is NULL to signal "needs slow-path I/O
// handling" (only $3000-$30FF and its 15 mirrors).
//
// Global linkage so the inline accessors in t7_internal.h reach them.
// Renamed to the public-API names that the header expects.
uint8_t *t7_read_page[256];
uint8_t *t7_write_page[256];

// Memory-mapped ROM (XIP-Flash). Wenn gesetzt, zeigt die Page-Table fuer den
// gebankten Bereich direkt in den Flash -> Bank-Switch ohne 32-KB-Kopie.
static const uint8_t *g_rom_mapped = NULL;
// Debug: Zahl der echten Bank-Wechsel (fuer On-Screen-Diagnose).
uint32_t t7_debug_bank_switches = 0;

// Sound: jeder Schreibzugriff auf $3050-$306F wird gezaehlt (Huellkurven-Trigger).
volatile uint32_t t7_snd_write_seq  = 0;
volatile uint8_t  t7_snd_last_addr  = 0;
volatile uint8_t  t7_snd_last_val   = 0;

uint32_t g_t7_static_off = 0;
uint8_t  g_t7_banks = 20;
/* Tamagotchi Friends: the ROM file holds 16 KB more at the start. Bank 0 begins at 0x4000 (static
 * ROM, then its code linked to $8000), bank n >= 1 at (n + 1) * 32 KB. Tama-Go: bank n at n * 32 KB. */
static inline size_t t7_bank_off(uint8_t b) { size_t o = (size_t)b * T7_ROM_BANK_SIZE; return g_t7_static_off ? (b ? o + T7_ROM_BANK_SIZE : 0x4000) : o; }

int      t7_is_friends(void)   { return g_t7_static_off != 0; }
uint16_t t7_rtc_sec_addr(void) { return g_t7_static_off ? 0x0136 : 0x0396; }   /* second of the game clock */

// Rebuild the entire page table. Called on init and on every ROM bank switch.
static void rebuild_page_table(void)
{
  // $0000-$0FFF — Work RAM (1.5 KB) mirrored.
  for (int p = 0; p < 16; p++) {
    int wram_page = p % 6;
    t7_read_page[p]  = g_wram + wram_page * 0x100;
    t7_write_page[p] = g_wram + wram_page * 0x100;
  }
  // $1000-$2FFF — Display RAM (512 B) mirrored across 32 pages.
  for (int p = 0x10; p < 0x30; p++) {
    t7_read_page[p]  = g_dram + (p & 1) * 0x100;
    t7_write_page[p] = g_dram + (p & 1) * 0x100;
  }
  // $3000-$3FFF — I/O registers. NULL = slow path.
  for (int p = 0x30; p < 0x40; p++) {
    t7_read_page[p]  = NULL;
    t7_write_page[p] = NULL;
  }
  // $4000-$BFFF — Banked ROM (zero-copy aus dem gemappten Flash falls verfuegbar).
  {
    const uint8_t *bankbase = g_rom_mapped
        ? (g_rom_mapped + t7_bank_off(g_rom_bank_id))
        : g_rom_bank_buf;
    for (int p = 0x40; p < 0xC0; p++) {
      t7_read_page[p]  = (uint8_t *)(bankbase + (p - 0x40) * 0x100);
      t7_write_page[p] = NULL;
    }
  }
  // $C000-$FFFF — Static ROM.
  for (int p = 0xC0; p < 0x100; p++) {
    t7_read_page[p]  = g_static_rom + (p - 0xC0) * 0x100;
    t7_write_page[p] = NULL;
  }
}

// ----- ROM banking --------------------------------------------------------


bool t7_load_static_rom(void)
{
  ResHandle h = resource_get_handle(RESOURCE_ID_TAMA_ROM);
  if (!h) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "t7emu: ROM resource not found");
    return false;
  }
  size_t total = resource_size(h);
  if (total < T7_STATIC_ROM_SIZE) {
    APP_LOG(APP_LOG_LEVEL_ERROR,
            "t7emu: ROM too small (%u < %u)",
            (unsigned)total, (unsigned)T7_STATIC_ROM_SIZE);
    return false;
  }

  /* Tama-Go: static ROM at 0, 20 banks. Tamagotchi Friends: its reset vector is not at
   * 0x3FFC but at 0x7FFC, the static ROM is at 0x4000, 23 banks (see t7_bank_off). */
  g_t7_static_off = 0; g_t7_banks = 20;
  if (total >= 0x8000) {
    uint8_t v[4];
    resource_load_byte_range(h, 0x3FFC, v, 2); resource_load_byte_range(h, 0x7FFC, v + 2, 2);
    if (!(v[0] | v[1]) && (v[2] | v[3])) { g_t7_static_off = 0x4000; g_t7_banks = 23; }
  }
  size_t n = resource_load_byte_range(h, g_t7_static_off, g_static_rom,
                                      T7_STATIC_ROM_SIZE);
  if (n != T7_STATIC_ROM_SIZE) {
    APP_LOG(APP_LOG_LEVEL_ERROR,
            "t7emu: short read on static ROM (%u/%u)",
            (unsigned)n, (unsigned)T7_STATIC_ROM_SIZE);
    return false;
  }

  APP_LOG(APP_LOG_LEVEL_INFO,
          "t7emu: static ROM loaded (%u bytes), total ROM size %u",
          (unsigned)n, (unsigned)total);

  // Build IRQ vector table from $FFC0-$FFDF.
  for (int i = 0; i < 16; i++) {
    uint16_t lo = g_static_rom[0x3FC0 + i * 2];
    uint16_t hi = g_static_rom[0x3FC0 + i * 2 + 1];
    g_irq_vectors[i] = lo | (hi << 8);
  }

  g_rom_loaded = true;
  g_rom_resource = h;

  // Falls der ROM direkt im Flash adressierbar ist: zero-copy Banking.
  g_rom_mapped = t7_compat_rom_mapped();
  if (g_rom_mapped)
    APP_LOG(APP_LOG_LEVEL_INFO, "t7emu: ROM memory-mapped (zero-copy banking)");

  // Build the page table now that all backing buffers exist.
  rebuild_page_table();
  return true;
}

void T7_HOT t7_set_rom_bank(uint8_t bank)
{
  uint8_t actual = bank % g_t7_banks;
  if (actual == g_rom_bank_id && g_rom_loaded) return;
  if (!g_rom_loaded) return;
  g_rom_bank_id = actual;
  t7_debug_bank_switches++;

  if (g_rom_mapped) {
    // Zero-copy: Page-Table-Zeiger auf die neue Bank im Flash umsetzen.
    const uint8_t *bankbase = g_rom_mapped + t7_bank_off(actual);
    for (int p = 0x40; p < 0xC0; p++)
      t7_read_page[p] = (uint8_t *)(bankbase + (p - 0x40) * 0x100);
    return;
  }

  // Fallback (Host): Bank in den RAM-Puffer kopieren.
  size_t offset = t7_bank_off(actual);
  size_t n = resource_load_byte_range(g_rom_resource, offset,
                                      g_rom_bank_buf, T7_ROM_BANK_SIZE);
  if (n != T7_ROM_BANK_SIZE)
    APP_LOG(APP_LOG_LEVEL_ERROR, "t7emu: bank %u short read (%u/%u)",
            actual, (unsigned)n, (unsigned)T7_ROM_BANK_SIZE);
}

// ----- I/O register handlers ----------------------------------------------

static void io_write_bank(uint8_t reg, uint8_t value)
{
  g_cpureg[reg] = value;
  t7_set_rom_bank(value);
}

// $3073, $3074: IRQ pending flags. Writes acknowledge (clear) pending bits.
static void io_write_int_flag(uint8_t reg, uint8_t value)
{
  if (ir_trace_armed) spi_log(1, reg, value);   // $3073/$3074 Int-Flags (IR-IRQ ack)
  g_cpureg[reg] &= ~value;
  // Recompute the fast-path cache flag.
  g_irq_pending_any = (g_cpureg[0x73] | g_cpureg[0x74]) != 0;
}

static uint8_t s_last_csn = 0xFF;
static void io_write_porta(uint8_t reg, uint8_t value)
{
  g_cpureg[reg] = value;
  (void)s_last_csn;   // PortA-Logging entfernt (war nur Rauschen + Serial-Flut)
}

static uint8_t io_read_porta(uint8_t reg)
{
  (void)reg;
  uint8_t mask  = g_cpureg[0x11];
  uint8_t value = g_cpureg[0x12];
  uint8_t spi_power = mask & value & 0x10;
  uint8_t inserted_figure = g_figure_id;
  uint8_t input = g_keys | ((spi_power ? 0 : inserted_figure) << 5);
  return (mask & value) | (~mask & input);
}

// $3015 (direction) and $3016 (data) writes both end up here. After
// updating the register, we feed the new bus state to the EEPROM bit
// banging engine. Matches write_portb_dir_data in registers.js exactly:
//
//   d = ~mask | data    (input bits read as 1 because of pull-ups)
//   eeprom.update(d & 4, d & 2, d & 1)
//
// Bit 2 = POWER, bit 1 = CLK, bit 0 = DATA.
static void io_write_portb(uint8_t reg, uint8_t value)
{
  g_cpureg[reg] = value;
  if (reg == 0x16) spi_cs_set(value & 0x10);   // $3016 Bit 4 = SPI-Figur-CS (aktiv-LOW)
  uint8_t mask = g_cpureg[0x15];
  uint8_t d    = (uint8_t)(~mask) | g_cpureg[0x16];
  t7_eeprom_update(d & 4, d & 2, d & 1);
}

// Port B read: bits configured as outputs return what we drove; bits
// configured as inputs read from the EEPROM data line.
//
// (As in the JS reference, the EEPROM output bit is broadcast over all
// input bits via the ~mask term. Only bit 0 is actually meaningful for
// the EEPROM but the firmware only reads bit 0 anyway.)
static uint8_t io_read_portb(uint8_t reg)
{
  uint8_t mask        = g_cpureg[0x15];
  uint8_t eeprom_out  = t7_eeprom_output();
  uint8_t input_bits  = eeprom_out ? (uint8_t)~mask : 0;
  return (uint8_t)((mask & g_cpureg[reg]) | input_bits);
}

// Slow-path I/O read — called only when t7_read_page[page] is NULL.
// Renamed from io_read to t7_io_read so the public inline in
// t7_internal.h can call it.
uint8_t T7_HOT t7_io_read(uint16_t addr)
{
  uint8_t reg = addr & 0xFF;
  switch (reg) {
    case 0x12: return io_read_porta(reg);
    case 0x16: return io_read_portb(reg);
    default:
      if (reg >= 0xB0 && reg <= 0xBA) {            // SPI-Registerbereich ($30B0-$30BA)
        uint8_t r;
        if      (reg == 0xB6) r = spi_rx();         // P_SPI_Read  -> Figur-ROM-Daten
        else if (reg == 0xB7) r = g_figure_id ? 0x04 : g_cpureg[reg]; // Status Bit2 = bereit
        else                  r = g_cpureg[reg];
        spi_log(0, reg, r);                          // jetzt auch B6 loggen (Diagnose)
        return r;
      }
      if (ir_trace_armed && reg >= 0x70 && reg <= 0x7F) spi_log(0, reg, g_cpureg[reg]); // IR-RX/Status
      return g_cpureg[reg];
  }
}

// Slow-path I/O write — called only when t7_write_page[page] is NULL.
void T7_HOT t7_io_write(uint16_t addr, uint8_t value)
{
  uint8_t reg = addr & 0xFF;
  switch (reg) {
    case 0x00: io_write_bank(reg, value);     break;
    case 0x11: io_write_porta(reg, value);    break;
    case 0x12: io_write_porta(reg, value);    break;
    case 0x15: io_write_portb(reg, value);    break;
    case 0x16: io_write_portb(reg, value);    break;
    case 0x73: io_write_int_flag(reg, value); break;
    case 0x74: io_write_int_flag(reg, value); break;
    default:
      if (reg >= 0xB0 && reg <= 0xBA) {            // SPI-Registerbereich ($30B0-$30BA)
        spi_log(1, reg, value);
        if (reg == 0xB3) spi_tx(value);            // P_SPI_Write -> Kommando/Adresse
        g_cpureg[reg] = value;
        break;
      }
      if (reg >= 0x50 && reg <= 0x6F) {       // Sound-Registerbereich
        t7_snd_write_seq++;
        t7_snd_last_addr = reg;
        t7_snd_last_val  = value;
      }
      if (ir_trace_armed && reg >= 0x70 && reg <= 0x7F) spi_log(1, reg, value); // IR-TX/Ctrl
      g_cpureg[reg] = value;
      // Originalton: Timer ($3031-$3035) und DAC ($3060/$3065) mitfuehren
      if (g_snd_on && ((reg >= 0x30 && reg <= 0x35) || reg == 0x60 || reg == 0x65))
        t7_snd_io_write(reg, value);
      break;
  }
}

// Sound investigation note (kept here for reference):
//   $3060/$3062/$3064/$3065 are written in lockstep 4-tuples — strong
//   candidate for SPU channel control. Re-add an I/O write counter here
//   if/when sound work resumes. Removed to keep hot-path lean and the
//   APP_LOG quiet.

// Roh-Auslese eines I/O-Registers ($3000-Seite) fuers Sound-Experiment.
uint8_t t7_debug_ioreg(uint8_t reg) { return g_cpureg[reg]; }
uint32_t t7_debug_snd_seq(void) { return t7_snd_write_seq; }
uint8_t  t7_debug_snd_addr(void) { return t7_snd_last_addr; }
uint8_t  t7_debug_snd_val(void) { return t7_snd_last_val; }

// ----- IRQ / NMI dispatch helpers ----------------------------------------

void t7_fire_irq(uint8_t i)
{
  PROFILE_INC(irqs);
  uint16_t mask = (g_cpureg[0x70] << 8) | g_cpureg[0x71];
  if (!((0x8000 >> i) & mask)) return;
  g_cpureg[0x73 + (i >> 3)] |= 0x80 >> (i & 7);
  // Set the fast-path cache flag so the CPU step sees it cheaply.
  g_irq_pending_any = 1;
}

void t7_fire_nmi(uint8_t i)
{
  PROFILE_INC(nmis);
  if (~g_cpureg[0x76] & (0x80 >> i)) return;
  t7_cpu_nmi();
}
