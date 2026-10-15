/*
 * t7_cpu.c - 6502 CPU emulator for V7
 *
 * Port of asterick/tamago's JavaScript 6502 core to C. The original code:
 *   src/cpu/6502.js        — fetch loop, IRQ, NMI, reset
 *   src/cpu/address.js     — 13 addressing modes
 *   src/cpu/operations.js  — 56 instruction implementations
 *   src/data/instructions.js — 256-entry opcode dispatch table
 *
 * Differences from the JS original:
 *   - Flags stored as separate booleans (c, z, i, d, v, n) matching the
 *     JS style. Slightly slower than packed P-register but readable.
 *   - Opcode dispatch is a switch instead of a function-pointer table.
 *     ARM's compiler turns dense switches into branch tables, which is
 *     fast and saves the indirect-call overhead.
 *   - Addressing modes are inlined as macros where it makes sense, to
 *     avoid function-call overhead in the inner loop.
 *
 * The CPU calls into t7_read / t7_write (defined in
 * t7_memory.c) for every memory access.
 */

#include "t7_internal.h"

// Pebble's default -Os is wrong for this file — every static-inline op_*
// and addressing-mode helper benefits from -O2 unrolling/inlining. We
// don't apply it via per-function attribute because the inline helpers
// are called from multiple places; a file-level pragma covers them all.
#pragma GCC optimize ("O2")

// Convenience pointers to cut down on g_sys-> noise in this file.
#define CPU g_cpu

// Status-flag fields, stored as separate uint8_t for clarity. Non-zero
// means set; the actual bit values don't matter as long as we use them
// consistently. The packed P register is only assembled on PHP/RTI etc.
//
// We keep them inside cpu6502_t.p as packed bits because that's simpler
// for serialization. Accessor macros pack/unpack as needed.
//
// NOTE: We pack/unpack on every flag set/get. To avoid that overhead,
// we keep a *separate* flag set in static variables here and only sync
// to cpu.p when push/pull/reset/etc. needs it. This matches the JS
// original which has separate cpu.c/cpu.z/... fields.

// NZ flags stored lazily as byte values.
//   N flag: (s_nz_n & 0x80) != 0       — bit 7 of the last "N source"
//   Z flag: s_nz_z == 0                 — last "Z source" was zero
//
// Two bytes (not one) because BIT and PLP can set N and Z from
// independent sources (e.g. BIT: N from v[7], Z from (A & v) == 0).
// Storing just one byte loses the N=1+Z=1 case that BIT routinely
// produces, which breaks branches after BIT — exactly the path the
// Tama firmware uses to test status bits.
//
// set_nz (the common case) updates both with the same value, which the
// compiler may even fuse into a single 16-bit store.
static uint8_t s_nz_n;     // value whose bit 7 is the N flag
static uint8_t s_nz_z;     // value whose zero-ness is the Z flag
static uint8_t s_flag_v;   // Overflow
static uint8_t s_flag_d;  // Decimal
static uint8_t s_flag_i;  // IRQ disable
static uint8_t s_flag_c;  // Carry

// Pack the six flags into a P-register byte. Bits 4 (B) and 5 (U) are
// the "always set" pair on push; we set them here too.
static inline uint8_t pack_p(void)
{
  return (s_flag_c ? 0x01 : 0) |
         ((s_nz_z == 0) ? 0x02 : 0) |
         (s_flag_i ? 0x04 : 0) |
         (s_flag_d ? 0x08 : 0) |
         0x20 |
         (s_flag_v ? 0x40 : 0) |
         (s_nz_n & 0x80);
}

// Unpack a P-register byte into the flags. N and Z are restored
// independently into their respective lazy bytes.
static inline void unpack_p(uint8_t p)
{
  s_flag_c = p & 0x01;
  s_flag_i = p & 0x04;
  s_flag_d = p & 0x08;
  s_flag_v = p & 0x40;
  s_nz_n = (p & 0x80) ? 0x80 : 0;    // N flag → bit 7 of s_nz_n
  s_nz_z = (p & 0x02) ? 0    : 1;    // Z flag → s_nz_z == 0
}

// Hot path — called by ~35% of opcodes. Two byte stores, no computes.
// The compiler may fuse these into a single 16-bit halfword store if
// s_nz_n and s_nz_z are adjacent in memory (they are).
static inline void set_nz(uint8_t v)
{
  s_nz_n = v;
  s_nz_z = v;
}

// ----- Stack operations ---------------------------------------------------

static inline void push(uint8_t v)
{
  t7_write(0x0100 | CPU.s, v);
  CPU.s = (CPU.s - 1) & 0xFF;
}

static inline uint8_t pull(void)
{
  CPU.s = (CPU.s + 1) & 0xFF;
  return t7_read(0x0100 | CPU.s);
}

// ----- Fetch operations ---------------------------------------------------

static inline uint8_t fetch(void)
{
  uint8_t d = t7_read(CPU.pc);
  CPU.pc = (CPU.pc + 1) & 0xFFFF;
  return d;
}

static inline uint16_t fetch16(void)
{
  uint8_t l = fetch();
  uint8_t h = fetch();
  return l | (h << 8);
}

// ----- IRQ / NMI / Reset --------------------------------------------------

void t7_cpu_reset(void)
{
  CPU.a = 0;
  CPU.x = 0;
  CPU.y = 0;
  CPU.s = 0xFF;  // initial stack pointer; some 6502 conventions use 0xFF
  s_nz_n = 0;      // N=0
  s_nz_z = 1;      // Z=0 (non-zero)
  s_flag_v = 0;
  s_flag_d = 0;
  s_flag_i = 1;  // IRQs masked at reset
  s_flag_c = 0;
  CPU.p = pack_p();
  // Reset vector at $FFFC
  CPU.pc = t7_read16(0xFFFC);
}

void t7_cpu_nmi(void)
{
  PROFILE_INC(nmi_entries);
  push(CPU.pc >> 8);
  push(CPU.pc & 0xFF);
  push(pack_p());
  CPU.pc = t7_read16(0xFFFA);
}

// IRQ entry: `brk` is true for BRK (software interrupt), which sets the
// B flag in the pushed P register but doesn't affect the running flags.
// IRQ dispatch via the priority-encoded vector happens in t7_memory.c.
static void cpu_irq_internal(bool brk)
{
  push(CPU.pc >> 8);
  push(CPU.pc & 0xFF);
  push(pack_p() | (brk ? 0x10 : 0));
  // For BRK, jump to the standard IRQ vector at $FFFE. For hardware IRQs
  // we use the priority-encoded vector; see t7_cpu_irq below.
  CPU.pc = t7_read16(0xFFFE);
  s_flag_i = 1;
}

void t7_cpu_irq(void)
{
  PROFILE_INC(irq_entries);
  // Hardware IRQ: look up the highest-priority pending bit in
  // cpureg[$73:$74] (16-bit) and jump to the corresponding vector.
  // The vector indices are 0..15; index 0 is highest priority.
  uint16_t pending = (g_cpureg[0x73] << 8) | g_cpureg[0x74];
  if (!pending) return;

  // Find highest set bit (0..15) via count-leading-zeros.
  // __builtin_clz operates on 32-bit values; we shift to align bit 15
  // of `pending` to bit 31 of the input so the result is the bit index
  // (0 = bit 15 of pending = highest priority).
  int idx = __builtin_clz((uint32_t)pending << 16);
  // idx is now 0..15 where 0 = bit 15 of pending was set.

  push(CPU.pc >> 8);
  push(CPU.pc & 0xFF);
  push(pack_p());
  CPU.pc = g_irq_vectors[idx];
  s_flag_i = 1;
  // The ROM's IRQ handler will write to $3074 to clear the pending bit
  // (which our io_write_int_flag refreshes g_irq_pending_any from). But
  // for safety, refresh here too — covers cases where the cache might
  // be out of sync.
  g_irq_pending_any = (g_cpureg[0x73] | g_cpureg[0x74]) != 0;
}

// ----- Addressing mode resolvers ------------------------------------------
//
// Each resolver returns the effective address for the operation. For
// implied/accumulator modes there is no memory address — the operations
// that use these modes don't dereference the returned value.

static inline uint16_t addr_implied(void)    { return 0; }
static inline uint16_t addr_accumulator(void){ return 0; }
static inline uint16_t addr_immediate(void)  { uint16_t a = CPU.pc; CPU.pc = (CPU.pc+1) & 0xFFFF; return a; }
static inline uint16_t addr_zeropage(void)   { return fetch(); }
static inline uint16_t addr_zeropageX(void)  { return (fetch() + CPU.x) & 0xFF; }
static inline uint16_t addr_zeropageY(void)  { return (fetch() + CPU.y) & 0xFF; }
static inline uint16_t addr_absolute(void)   { return fetch16(); }
static inline uint16_t addr_absoluteX(void)  { return (fetch16() + CPU.x) & 0xFFFF; }
static inline uint16_t addr_absoluteY(void)  { return (fetch16() + CPU.y) & 0xFFFF; }

static inline uint16_t addr_relative(void) {
  // Sign-extend the 8-bit offset.
  int8_t offset = (int8_t)fetch();
  return (CPU.pc + offset) & 0xFFFF;
}

static inline uint16_t addr_indirect(void) {
  // Classic 6502 page-wrap bug: the high byte read wraps within the
  // same page as the low byte. (See: JMP ($xxFF) doesn't cross the page.)
  uint16_t ptr = fetch16();
  uint8_t lo = t7_read(ptr);
  uint8_t hi = t7_read((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
  return lo | (hi << 8);
}

static inline uint16_t addr_indirectX(void) {
  uint8_t zp = (fetch() + CPU.x) & 0xFF;
  uint8_t lo = t7_read(zp);
  uint8_t hi = t7_read((zp + 1) & 0xFF);
  return lo | (hi << 8);
}

static inline uint16_t addr_indirectY(void) {
  uint8_t zp = fetch();
  uint8_t lo = t7_read(zp);
  uint8_t hi = t7_read((zp + 1) & 0xFF);
  return ((lo | (hi << 8)) + CPU.y) & 0xFFFF;
}

// ----- Instruction implementations ----------------------------------------
//
// Each instruction takes the effective address `addr`. For
// implied/accumulator-mode ops `addr` is ignored.

static inline void op_LDA(uint16_t addr) { CPU.a = t7_read(addr); set_nz(CPU.a); }
static inline void op_LDX(uint16_t addr) { CPU.x = t7_read(addr); set_nz(CPU.x); }
static inline void op_LDY(uint16_t addr) { CPU.y = t7_read(addr); set_nz(CPU.y); }
static inline void op_STA(uint16_t addr) { t7_write(addr, CPU.a); }
static inline void op_STX(uint16_t addr) { t7_write(addr, CPU.x); }
static inline void op_STY(uint16_t addr) { t7_write(addr, CPU.y); }

static inline void op_TAX(void) { CPU.x = CPU.a; set_nz(CPU.x); }
static inline void op_TAY(void) { CPU.y = CPU.a; set_nz(CPU.y); }
static inline void op_TXA(void) { CPU.a = CPU.x; set_nz(CPU.a); }
static inline void op_TYA(void) { CPU.a = CPU.y; set_nz(CPU.a); }
static inline void op_TSX(void) { CPU.x = CPU.s; set_nz(CPU.x); }
static inline void op_TXS(void) { CPU.s = CPU.x; }

static inline void op_INX(void) { CPU.x = (CPU.x + 1) & 0xFF; set_nz(CPU.x); }
static inline void op_INY(void) { CPU.y = (CPU.y + 1) & 0xFF; set_nz(CPU.y); }
static inline void op_DEX(void) { CPU.x = (CPU.x - 1) & 0xFF; set_nz(CPU.x); }
static inline void op_DEY(void) { CPU.y = (CPU.y - 1) & 0xFF; set_nz(CPU.y); }

static inline void op_INC(uint16_t addr) {
  uint8_t v = (t7_read(addr) + 1) & 0xFF;
  set_nz(v);
  t7_write(addr, v);
}
static inline void op_DEC(uint16_t addr) {
  uint8_t v = (t7_read(addr) - 1) & 0xFF;
  set_nz(v);
  t7_write(addr, v);
}

static inline void op_AND(uint16_t addr) { CPU.a &= t7_read(addr); set_nz(CPU.a); }
static inline void op_ORA(uint16_t addr) { CPU.a |= t7_read(addr); set_nz(CPU.a); }
static inline void op_EOR(uint16_t addr) { CPU.a ^= t7_read(addr); set_nz(CPU.a); }

static inline void op_BIT(uint16_t addr) {
  uint8_t v = t7_read(addr);
  s_flag_v = v & 0x40;
  s_nz_n   = v;          // N from v[7]
  s_nz_z   = CPU.a & v;  // Z from (A & v) == 0
}

// Shifts: when `acc` is true, operate on accumulator (addr ignored).
// We keep ASL/LSR/ROL/ROR as two variants — *_acc for accumulator,
// regular for memory.
static inline void op_ASL_mem(uint16_t addr) {
  uint8_t d = t7_read(addr);
  uint8_t o = (d << 1) & 0xFF;
  s_flag_c = d & 0x80;
  set_nz(o);
  t7_write(addr, o);
}
static inline void op_ASL_acc(void) {
  uint8_t o = (CPU.a << 1) & 0xFF;
  s_flag_c = CPU.a & 0x80;
  CPU.a = o;
  set_nz(o);
}
static inline void op_LSR_mem(uint16_t addr) {
  uint8_t d = t7_read(addr);
  uint8_t o = d >> 1;
  s_flag_c = d & 0x01;
  set_nz(o);
  t7_write(addr, o);
}
static inline void op_LSR_acc(void) {
  uint8_t o = CPU.a >> 1;
  s_flag_c = CPU.a & 0x01;
  CPU.a = o;
  set_nz(o);
}
static inline void op_ROL_mem(uint16_t addr) {
  uint8_t d = t7_read(addr);
  uint8_t o = ((d << 1) & 0xFF) | (s_flag_c ? 1 : 0);
  s_flag_c = d & 0x80;
  set_nz(o);
  t7_write(addr, o);
}
static inline void op_ROL_acc(void) {
  uint8_t o = ((CPU.a << 1) & 0xFF) | (s_flag_c ? 1 : 0);
  s_flag_c = CPU.a & 0x80;
  CPU.a = o;
  set_nz(o);
}
static inline void op_ROR_mem(uint16_t addr) {
  uint8_t d = t7_read(addr);
  uint8_t o = (d >> 1) | (s_flag_c ? 0x80 : 0);
  s_flag_c = d & 0x01;
  set_nz(o);
  t7_write(addr, o);
}
static inline void op_ROR_acc(void) {
  uint8_t o = (CPU.a >> 1) | (s_flag_c ? 0x80 : 0);
  s_flag_c = CPU.a & 0x01;
  CPU.a = o;
  set_nz(o);
}

// ADC/SBC: full implementation including decimal mode (matches JS original
// exactly). Most ROMs don't use decimal mode but we include it for safety.
static inline void op_ADC(uint16_t addr) {
  uint8_t data = t7_read(addr);
  uint16_t o = CPU.a + data + (s_flag_c ? 1 : 0);
  // Overflow: same sign in A and data, different sign in result
  s_flag_v = (~(CPU.a ^ data) & (CPU.a ^ o)) & 0x80;
  set_nz(o & 0xFF);

  if (s_flag_d) {
    int al = (CPU.a & 0x0F) + (data & 0x0F) + (s_flag_c ? 1 : 0);
    int ah = (CPU.a & 0xF0) + (data & 0xF0) + ((al >= 0x10) ? 0x10 : 0);
    if (al > 0x09) al += 0x06;
    if (ah > 0x90) ah += 0x60;
    o = (al & 0x0F) + ah;
  }

  s_flag_c = (o > 0xFF) ? 1 : 0;
  CPU.a = o & 0xFF;
}
static inline void op_SBC(uint16_t addr) {
  uint8_t data = t7_read(addr);
  int o = CPU.a - data - (s_flag_c ? 0 : 1);
  s_flag_v = ((CPU.a ^ data) & (CPU.a ^ o)) & 0x80;
  set_nz(o & 0xFF);
  s_flag_c = (o >= 0) ? 1 : 0;

  if (s_flag_d) {
    int al = (CPU.a & 0x0F) - (data & 0x0F) - (s_flag_c ? 0 : 1);
    int ah = (CPU.a & 0xF0) - (data & 0xF0) - ((al < 0) ? 0x10 : 0);
    if (al < 0) al -= 0x06;
    if (ah < 0) ah -= 0x60;
    o = (al & 0x0F) + ah;
  }
  CPU.a = o & 0xFF;
}

static inline void op_CMP(uint16_t addr) {
  uint8_t d = t7_read(addr);
  s_flag_c = CPU.a >= d;
  set_nz((CPU.a - d) & 0xFF);
}
static inline void op_CPX(uint16_t addr) {
  uint8_t d = t7_read(addr);
  s_flag_c = CPU.x >= d;
  set_nz((CPU.x - d) & 0xFF);
}
static inline void op_CPY(uint16_t addr) {
  uint8_t d = t7_read(addr);
  s_flag_c = CPU.y >= d;
  set_nz((CPU.y - d) & 0xFF);
}

// Flag operations
static inline void op_CLC(void) { s_flag_c = 0; }
static inline void op_SEC(void) { s_flag_c = 1; }
static inline void op_CLI(void) { s_flag_i = 0; }
static inline void op_SEI(void) { s_flag_i = 1; }
static inline void op_CLV(void) { s_flag_v = 0; }
static inline void op_CLD(void) { s_flag_d = 0; }
static inline void op_SED(void) { s_flag_d = 1; }

// Stack ops
static inline void op_PHA(void) { push(CPU.a); }
static inline void op_PLA(void) { CPU.a = pull(); set_nz(CPU.a); }
static inline void op_PHP(void) { push(pack_p() | 0x10); }
static inline void op_PLP(void) { unpack_p(pull()); }

// Jumps & branches
static inline void op_JMP(uint16_t addr) { CPU.pc = addr; }
static inline void op_JSR(uint16_t addr) {
  // PC has already advanced past the 2-byte operand; push PC-1.
  uint16_t r = (CPU.pc - 1) & 0xFFFF;
  push(r >> 8);
  push(r & 0xFF);
  CPU.pc = addr;
}
static inline void op_RTS(void) {
  uint8_t l = pull();
  uint8_t h = pull();
  CPU.pc = ((l | (h << 8)) + 1) & 0xFFFF;
}
static inline void op_RTI(void) {
  unpack_p(pull());
  uint8_t l = pull();
  uint8_t h = pull();
  CPU.pc = l | (h << 8);
}
static inline void op_BRK(void) {
  CPU.pc = (CPU.pc + 1) & 0xFFFF;
  cpu_irq_internal(true);
}

// Branch instructions take a relative offset and only jump if the
// condition is true.
#define BRANCH(cond) do { uint16_t a = addr_relative(); if (cond) CPU.pc = a; } while (0)

// ----- Main step function -------------------------------------------------
//
// Returns the cycle count of the executed instruction. The cycles table
// is generated from instructions.js — for unimplemented opcodes (illegal
// instructions on standard 6502) we return 2 and log a warning.

// Cycle counts indexed by opcode. Generated from data/instructions.js.
// 0 = illegal/unimplemented.
static const uint8_t opcode_cycles[256] = {
  [0x00] = 7, [0x01] = 6, [0x05] = 3, [0x06] = 5, [0x08] = 3, [0x09] = 2,
  [0x0A] = 2, [0x0D] = 4, [0x0E] = 6, [0x10] = 2, [0x11] = 5, [0x15] = 4,
  [0x16] = 6, [0x18] = 2, [0x19] = 4, [0x1D] = 4, [0x1E] = 7, [0x20] = 6,
  [0x21] = 6, [0x24] = 3, [0x25] = 3, [0x26] = 5, [0x28] = 4, [0x29] = 2,
  [0x2A] = 2, [0x2C] = 4, [0x2D] = 4, [0x2E] = 6, [0x30] = 2, [0x31] = 5,
  [0x35] = 4, [0x36] = 6, [0x38] = 2, [0x39] = 4, [0x3D] = 4, [0x3E] = 7,
  [0x40] = 6, [0x41] = 6, [0x45] = 3, [0x46] = 5, [0x48] = 3, [0x49] = 2,
  [0x4A] = 2, [0x4C] = 3, [0x4D] = 4, [0x4E] = 6, [0x50] = 2, [0x51] = 5,
  [0x55] = 4, [0x56] = 6, [0x58] = 2, [0x59] = 4, [0x5D] = 4, [0x5E] = 7,
  [0x60] = 6, [0x61] = 6, [0x65] = 3, [0x66] = 5, [0x68] = 4, [0x69] = 2,
  [0x6A] = 2, [0x6C] = 5, [0x6D] = 4, [0x6E] = 6, [0x70] = 2, [0x71] = 5,
  [0x75] = 4, [0x76] = 6, [0x78] = 2, [0x79] = 4, [0x7D] = 4, [0x7E] = 7,
  [0x81] = 6, [0x84] = 3, [0x85] = 3, [0x86] = 3, [0x88] = 2, [0x8A] = 2,
  [0x8C] = 4, [0x8D] = 4, [0x8E] = 4, [0x90] = 2, [0x91] = 6, [0x94] = 4,
  [0x95] = 4, [0x96] = 4, [0x98] = 2, [0x99] = 5, [0x9A] = 2, [0x9D] = 5,
  [0xA0] = 2, [0xA1] = 6, [0xA2] = 2, [0xA4] = 3, [0xA5] = 3, [0xA6] = 3,
  [0xA8] = 2, [0xA9] = 2, [0xAA] = 2, [0xAC] = 4, [0xAD] = 4, [0xAE] = 4,
  [0xB0] = 2, [0xB1] = 5, [0xB4] = 4, [0xB5] = 4, [0xB6] = 4, [0xB8] = 2,
  [0xB9] = 4, [0xBA] = 2, [0xBC] = 4, [0xBD] = 4, [0xBE] = 4, [0xC0] = 2,
  [0xC1] = 6, [0xC4] = 3, [0xC5] = 3, [0xC6] = 5, [0xC8] = 2, [0xC9] = 2,
  [0xCA] = 2, [0xCC] = 4, [0xCD] = 4, [0xCE] = 6, [0xD0] = 2, [0xD1] = 5,
  [0xD5] = 4, [0xD6] = 6, [0xD8] = 2, [0xD9] = 4, [0xDD] = 4, [0xDE] = 7,
  [0xE0] = 2, [0xE1] = 6, [0xE4] = 3, [0xE5] = 3, [0xE6] = 5, [0xE8] = 2,
  [0xE9] = 2, [0xEA] = 2, [0xEC] = 4, [0xED] = 4, [0xEE] = 6, [0xF0] = 2,
  [0xF1] = 5, [0xF5] = 4, [0xF6] = 6, [0xF8] = 2, [0xF9] = 4, [0xFD] = 4,
  [0xFE] = 7,
};

// `hot` attribute tells GCC this is a frequently-called function — it
// optimises it more aggressively (inlining decisions, branch ordering)
// and places it in a hot code section for better i-cache behaviour.
// Pebble builds with -Os by default (size-optimized). For this interpreter
// hot-loop -O2 unrolls the addressing mode inlines and the switch much
// better — measurably faster. Apply just to this function so we don't
// pay code-size cost elsewhere.
// === IR-BLE-Bridge: Paket-Inject-Trap ============================================
// Umgeht das gesamte IR-Physical-Layer (Carrier/Praeambel/Bit-Timing). Ein per
// BLE empfangenes 32-Byte-Paket wird direkt in den Empfangspuffer $0200 gelegt,
// die Receive-Routine ($72D0, gerufen bei $60C4) uebersprungen. Die Firmware
// validiert + verarbeitet das Paket nativ. Validiert: erzeugt "CONNECT".
static uint8_t  s_ir_rx_pkt[32];
static volatile int s_ir_rx_pending = 0;
static volatile int s_ir_bridge_en = 0;   // Bridge aktiv? (gated Completion-Fake)
static volatile uint32_t s_ir_activity = 0; // zaehlt $60C4-Polls (Verbindungsmenue aktiv)
static volatile int s_ir_tx_pending = 0;   // Firmware wartet auf Sende-Completion ($7926)

// TX-Completion: main.cpp pollt _pending und loest _complete zeitverzoegert aus
// (mimt die echte IR-Sendedauer ~150 ms -> Sende-Loop rast nicht).
int  t7_ir_tx_pending(void) { return s_ir_tx_pending; }
void t7_ir_tx_complete(void) { g_wram[0xA3] &= ~0x80; s_ir_tx_pending = 0; }

// Aktivitaetszaehler: steigt, solange die Firmware im IR-Verbindungsmenue ist.
uint32_t t7_ir_activity(void) { return s_ir_activity; }

// Aus dem Funk-Callback (oder Test-Trigger) aufrufen: Paket fuer die naechste
// Empfangsanfrage bereitstellen. Genau 32 Bytes erwartet.
void t7_ir_bridge_rx(const uint8_t *pkt32) {
  for (int i = 0; i < 32; i++) s_ir_rx_pkt[i] = pkt32[i];
  s_ir_rx_pending = 1;
}

// Bridge ein/aus: schaltet den TX-Completion-Fake ($7926) frei, damit der
// Sende-Deadlock nicht haengt. Im Normalbetrieb AUS (IR-Verhalten unveraendert).
void t7_ir_bridge_enable(int on) { s_ir_bridge_en = on; }

// TX-Capture: liefert das Sende-Paket aus $0220, wenn es gueltig (byte0&0F==2)
// und seit dem letzten Poll geaendert ist. 1 = neues Paket in out[32].
int t7_ir_bridge_tx_poll(uint8_t *out) {
  static uint8_t last[32];
  if ((g_wram[0x220] & 0x0F) != 2) return 0;
  int diff = 0;
  for (int i = 0; i < 32; i++) if (g_wram[0x220 + i] != last[i]) diff = 1;
  if (!diff) return 0;
  for (int i = 0; i < 32; i++) { last[i] = g_wram[0x220 + i]; out[i] = last[i]; }
  if (out[31] == 0) { int s = 0; for (int i = 0; i < 31; i++) s += out[i]; out[31] = (uint8_t)(s & 0xFF); }
  return 1;
}

#ifndef T7_FASTRUN
uint8_t T7_HOT t7_cpu_step(void) __attribute__((hot, optimize("O2")));

uint8_t t7_cpu_step(void)
{
  PROFILE_INC(opcodes);

  // IR-Bridge-Hooks: nur in Bank 3 (IR/Verbindung) ueberhaupt relevant. EIN
  // Bank-Check vorne -> im Normalbetrieb (andere Bank) genau EIN Vergleich pro
  // Instruktion statt sechs. Hot path (~4M Instr/s) -> spart spuerbar Emu-Zeit.
  if (g_rom_bank_id == 3) {
    if (CPU.pc == 0x60C4) {
      // $60C4 = IR-Empfangsanfrage (JSR $72D0). Wird nur im Verbindungsmenue/
      // STAND BY gepollt -> Zaehler fuer Auto-Aktivierung der Bridge.
      s_ir_activity++;
      // RX-Bridge: bereitgestelltes Paket einspeisen, echte Receive-Routine
      // ueberspringen.
      if (s_ir_rx_pending) {
        for (int i = 0; i < 32; i++) g_wram[0x200 + i] = s_ir_rx_pkt[i];
        g_wram[0x5A]   = 0x20;   // Byte-Zaehler = 32 (sonst von Receive gesetzt)
        g_wram[0x01B2] = 0x20;
        CPU.a   = 0x80;          // Rueckgabe "Paket empfangen"
        s_nz_z  = 0x80;          // Z = 0 (A != 0)
        s_flag_c = 0;            // C = 0  -> Handler nimmt den Verarbeitungspfad
        CPU.pc  = 0x60C7;        // JSR $72D0 ueberspringen
        s_ir_rx_pending = 0;
      }
    } else if (s_ir_bridge_en && CPU.pc == 0x7926) {
      // TX-Bridge: Firmware will senden (Busy-Wait auf $A3 bit7). Flag setzen;
      // main.cpp loest die Completion zeitverzoegert aus (~echte IR-Sendedauer).
      s_ir_tx_pending = 1;
    }
  }

  // Fire pending IRQs before fetching the next instruction.
  // Fast path: a single byte cache flag, updated by t7_fire_irq and
  // io_write_int_flag. Avoids reading cpureg[$73:$74] on every step.
  if (g_irq_pending_any && !s_flag_i) {
    t7_cpu_irq();
  }

  uint8_t opcode = fetch();
  uint8_t cycles = opcode_cycles[opcode];

  // Big switch dispatch. The compiler turns this into a branch table on
  // ARM since the cases are dense. Each case computes the addressing
  // mode (inline) and calls the instruction implementation.
  switch (opcode) {
    // --- LDA ---
    case 0xA9: op_LDA(addr_immediate());  break;
    case 0xA5: op_LDA(addr_zeropage());   break;
    case 0xB5: op_LDA(addr_zeropageX());  break;
    case 0xAD: op_LDA(addr_absolute());   break;
    case 0xBD: op_LDA(addr_absoluteX());  break;
    case 0xB9: op_LDA(addr_absoluteY());  break;
    case 0xA1: op_LDA(addr_indirectX());  break;
    case 0xB1: op_LDA(addr_indirectY());  break;
    // --- LDX ---
    case 0xA2: op_LDX(addr_immediate());  break;
    case 0xA6: op_LDX(addr_zeropage());   break;
    case 0xB6: op_LDX(addr_zeropageY());  break;
    case 0xAE: op_LDX(addr_absolute());   break;
    case 0xBE: op_LDX(addr_absoluteY());  break;
    // --- LDY ---
    case 0xA0: op_LDY(addr_immediate());  break;
    case 0xA4: op_LDY(addr_zeropage());   break;
    case 0xB4: op_LDY(addr_zeropageX());  break;
    case 0xAC: op_LDY(addr_absolute());   break;
    case 0xBC: op_LDY(addr_absoluteX());  break;
    // --- STA ---
    case 0x85: op_STA(addr_zeropage());   break;
    case 0x95: op_STA(addr_zeropageX());  break;
    case 0x8D: op_STA(addr_absolute());   break;
    case 0x9D: op_STA(addr_absoluteX());  break;
    case 0x99: op_STA(addr_absoluteY());  break;
    case 0x81: op_STA(addr_indirectX());  break;
    case 0x91: op_STA(addr_indirectY());  break;
    // --- STX/STY ---
    case 0x86: op_STX(addr_zeropage());   break;
    case 0x96: op_STX(addr_zeropageY());  break;
    case 0x8E: op_STX(addr_absolute());   break;
    case 0x84: op_STY(addr_zeropage());   break;
    case 0x94: op_STY(addr_zeropageX());  break;
    case 0x8C: op_STY(addr_absolute());   break;

    // --- Transfers ---
    case 0xAA: op_TAX(); break;
    case 0xA8: op_TAY(); break;
    case 0x8A: op_TXA(); break;
    case 0x98: op_TYA(); break;
    case 0xBA: op_TSX(); break;
    case 0x9A: op_TXS(); break;

    // --- INC/DEC register ---
    case 0xE8: op_INX(); break;
    case 0xC8: op_INY(); break;
    case 0xCA: op_DEX(); break;
    case 0x88: op_DEY(); break;
    // --- INC/DEC memory ---
    case 0xE6: op_INC(addr_zeropage());  break;
    case 0xF6: op_INC(addr_zeropageX()); break;
    case 0xEE: op_INC(addr_absolute());  break;
    case 0xFE: op_INC(addr_absoluteX()); break;
    case 0xC6: op_DEC(addr_zeropage());  break;
    case 0xD6: op_DEC(addr_zeropageX()); break;
    case 0xCE: op_DEC(addr_absolute());  break;
    case 0xDE: op_DEC(addr_absoluteX()); break;

    // --- Logical ---
    case 0x29: op_AND(addr_immediate()); break;
    case 0x25: op_AND(addr_zeropage());  break;
    case 0x35: op_AND(addr_zeropageX()); break;
    case 0x2D: op_AND(addr_absolute());  break;
    case 0x3D: op_AND(addr_absoluteX()); break;
    case 0x39: op_AND(addr_absoluteY()); break;
    case 0x21: op_AND(addr_indirectX()); break;
    case 0x31: op_AND(addr_indirectY()); break;
    case 0x09: op_ORA(addr_immediate()); break;
    case 0x05: op_ORA(addr_zeropage());  break;
    case 0x15: op_ORA(addr_zeropageX()); break;
    case 0x0D: op_ORA(addr_absolute());  break;
    case 0x1D: op_ORA(addr_absoluteX()); break;
    case 0x19: op_ORA(addr_absoluteY()); break;
    case 0x01: op_ORA(addr_indirectX()); break;
    case 0x11: op_ORA(addr_indirectY()); break;
    case 0x49: op_EOR(addr_immediate()); break;
    case 0x45: op_EOR(addr_zeropage());  break;
    case 0x55: op_EOR(addr_zeropageX()); break;
    case 0x4D: op_EOR(addr_absolute());  break;
    case 0x5D: op_EOR(addr_absoluteX()); break;
    case 0x59: op_EOR(addr_absoluteY()); break;
    case 0x41: op_EOR(addr_indirectX()); break;
    case 0x51: op_EOR(addr_indirectY()); break;
    case 0x24: op_BIT(addr_zeropage());  break;
    case 0x2C: op_BIT(addr_absolute());  break;

    // --- Shifts ---
    case 0x0A: op_ASL_acc();                  break;
    case 0x06: op_ASL_mem(addr_zeropage());   break;
    case 0x16: op_ASL_mem(addr_zeropageX());  break;
    case 0x0E: op_ASL_mem(addr_absolute());   break;
    case 0x1E: op_ASL_mem(addr_absoluteX());  break;
    case 0x4A: op_LSR_acc();                  break;
    case 0x46: op_LSR_mem(addr_zeropage());   break;
    case 0x56: op_LSR_mem(addr_zeropageX());  break;
    case 0x4E: op_LSR_mem(addr_absolute());   break;
    case 0x5E: op_LSR_mem(addr_absoluteX());  break;
    case 0x2A: op_ROL_acc();                  break;
    case 0x26: op_ROL_mem(addr_zeropage());   break;
    case 0x36: op_ROL_mem(addr_zeropageX());  break;
    case 0x2E: op_ROL_mem(addr_absolute());   break;
    case 0x3E: op_ROL_mem(addr_absoluteX());  break;
    case 0x6A: op_ROR_acc();                  break;
    case 0x66: op_ROR_mem(addr_zeropage());   break;
    case 0x76: op_ROR_mem(addr_zeropageX());  break;
    case 0x6E: op_ROR_mem(addr_absolute());   break;
    case 0x7E: op_ROR_mem(addr_absoluteX());  break;

    // --- Arithmetic ---
    case 0x69: op_ADC(addr_immediate()); break;
    case 0x65: op_ADC(addr_zeropage());  break;
    case 0x75: op_ADC(addr_zeropageX()); break;
    case 0x6D: op_ADC(addr_absolute());  break;
    case 0x7D: op_ADC(addr_absoluteX()); break;
    case 0x79: op_ADC(addr_absoluteY()); break;
    case 0x61: op_ADC(addr_indirectX()); break;
    case 0x71: op_ADC(addr_indirectY()); break;
    case 0xE9: op_SBC(addr_immediate()); break;
    case 0xE5: op_SBC(addr_zeropage());  break;
    case 0xF5: op_SBC(addr_zeropageX()); break;
    case 0xED: op_SBC(addr_absolute());  break;
    case 0xFD: op_SBC(addr_absoluteX()); break;
    case 0xF9: op_SBC(addr_absoluteY()); break;
    case 0xE1: op_SBC(addr_indirectX()); break;
    case 0xF1: op_SBC(addr_indirectY()); break;

    // --- Compares ---
    case 0xC9: op_CMP(addr_immediate()); break;
    case 0xC5: op_CMP(addr_zeropage());  break;
    case 0xD5: op_CMP(addr_zeropageX()); break;
    case 0xCD: op_CMP(addr_absolute());  break;
    case 0xDD: op_CMP(addr_absoluteX()); break;
    case 0xD9: op_CMP(addr_absoluteY()); break;
    case 0xC1: op_CMP(addr_indirectX()); break;
    case 0xD1: op_CMP(addr_indirectY()); break;
    case 0xE0: op_CPX(addr_immediate()); break;
    case 0xE4: op_CPX(addr_zeropage());  break;
    case 0xEC: op_CPX(addr_absolute());  break;
    case 0xC0: op_CPY(addr_immediate()); break;
    case 0xC4: op_CPY(addr_zeropage());  break;
    case 0xCC: op_CPY(addr_absolute());  break;

    // --- Flags ---
    case 0x18: op_CLC(); break;
    case 0x38: op_SEC(); break;
    case 0x58: op_CLI(); break;
    case 0x78: op_SEI(); break;
    case 0xB8: op_CLV(); break;
    case 0xD8: op_CLD(); break;
    case 0xF8: op_SED(); break;

    // --- Stack ---
    case 0x48: op_PHA(); break;
    case 0x68: op_PLA(); break;
    case 0x08: op_PHP(); break;
    case 0x28: op_PLP(); break;

    // --- Jumps ---
    case 0x4C: op_JMP(addr_absolute()); break;
    case 0x6C: op_JMP(addr_indirect()); break;
    case 0x20: op_JSR(addr_absolute()); break;
    case 0x60: op_RTS(); break;
    case 0x40: op_RTI(); break;
    case 0x00: op_BRK(); break;

    // --- Branches ---
    case 0x10: BRANCH(!(s_nz_n & 0x80)); break;  // BPL
    case 0x30: BRANCH( (s_nz_n & 0x80)); break;  // BMI
    case 0x50: BRANCH(!s_flag_v); break;  // BVC
    case 0x70: BRANCH( s_flag_v); break;  // BVS
    case 0x90: BRANCH(!s_flag_c); break;  // BCC
    case 0xB0: BRANCH( s_flag_c); break;  // BCS
    case 0xD0: BRANCH(s_nz_z != 0); break;  // BNE
    case 0xF0: BRANCH(s_nz_z == 0); break;  // BEQ

    // --- NOP and illegal ---
    case 0xEA: /* NOP */ break;

    default:
      // Illegal/undefined opcode. The standard NMOS 6502 has many of
      // these but the JS emulator throws on encountering them. We log
      // and treat as NOP so we don't crash mid-game on a ROM glitch.
      APP_LOG(APP_LOG_LEVEL_WARNING,
              "t7_cpu: illegal opcode 0x%02x at PC=$%04x",
              opcode, (CPU.pc - 1) & 0xFFFF);
      cycles = 2;
      break;
  }

  return cycles;
}
#endif
uint8_t t7_dbg_p(void) { return pack_p(); }
#include "t7_run.inc"
