/* v3cpu.c - Tamagotchi Connection V3 core: Sunplus SPLB32 (6502), ported from BrickEmuPy cores/SPLB32.py and
 * peripherals/HT24LC08.py by azya52 (CC0). The instructions are generated from the Python (v3/gen/py2c.py ->
 * v3ops.inc); the rest follows the Python line by line, with time in ticks of 1/128 cycle. Author: mameise */
#include "v3cpu.h"
#ifndef V3_FREESTANDING
#include <string.h>
#else
void *memcpy(void *d, const void *s, unsigned int n); void *memset(void *d, int c, unsigned int n); void *memmove(void *d, const void *s, unsigned int n);
#endif

#define SUB_DIV 15625u                  /* 4 MHz / 32768 Hz in ticks */
#define T1K_DIV 500000u                 /* SUB_DIV * 32 */
#define VADDR_NMI 0x7FFA
#define VADDR_RESET 0x7FFC
#define VADDR_IRQ 0x7FFE
#define RAM_OFF 0x40
#define RAM_SIZE 0x4C0
#define DP_OFF 0x3E00

/* ---- state (the names of the Python, so the generated instructions read the same) ---- */
static const uint8_t *g_rom; static uint32_t g_rom_size, g_rom_mask;
static int A, X, Y, SP, PC, NF, VF, BF, DF, IF, ZF, CF;
typedef struct {
  uint8_t ram[RAM_SIZE], dpram[V3_DPRAM];
  uint32_t rom_bank;                    /* << 15 */
  uint8_t cpu_enbl, cpu_div, clk32k, lcd_ctrl1;
  uint8_t wakeup_ctrl, wakeupreq, time_base, timer_ctrl;
  uint8_t int_ctrl, ireq, nmi_ctrl, nmireq;
  uint8_t a0c, a0d, a1c, a1d, byte_mirror, nibble_swap;
  uint8_t pdir[4], attr[4], platch[4], pin0[4], pin1[4];   /* PA PB PC PD */
  int64_t tm0_cnt, tm1_cnt, t1k_cnt;   /* ticks */
  int64_t tm0, tm1;                    /* ticks (count << 7) */
  uint32_t tm0_rel, tm1_rel, t1khz;
  uint32_t pend, next;                 /* ticks run but not yet given to the timers; ticks to their next event */
  uint16_t addr_irq, addr_nmi;
  uint8_t snd_env[2], snd_tog[2];
  /* EEPROM HT24LC08 */
  uint8_t ee[V3_EE_SIZE];
  uint8_t ee_state, ee_bits, ee_scl, ee_sda, ee_ack, ee_rx, ee_tx, ee_dirty;
  uint16_t ee_addr;
  /* infrared CON_V3_IR (peripherals/CON_V3_IR.py): bytes as pulse trains, in ticks */
  int32_t ir_txb, ir_txs, ir_rxb, ir_rxs;
  uint8_t ir_txbyte, ir_txbits, ir_rxstep, ir_rxbyte, ir_rxbits, ir_prev, ir_rxn, ir_txn;
  uint8_t ir_rxq[64], ir_txq[64];
} V3S;
static V3S S;
enum { PA, PB, PCP, PD };
static const uint8_t PULLUP[4] = { 0, 0, 0, 6 };   /* mask option: PD pull-ups (bits 1, 2) */

static inline uint8_t rom_byte(uint32_t a) { return g_rom_mask ? g_rom[a & g_rom_mask] : g_rom[a % g_rom_size]; }
static int port_read(int p)
{
  return ((~S.pdir[p] & (S.pin1[p] | (PULLUP[p] & ~S.pin0[p]))) | (S.pdir[p] & S.platch[p] & (PULLUP[p] | ~S.attr[p]))) & 0xFF;
}
/* a device drives a pin: level -1 released, 0 low, 1 high (interconnect.emit_port to the CPU) */
static void cpu_port_in(int p, int mask, int level)
{
  S.pin0[p] &= (uint8_t)~mask; S.pin1[p] &= (uint8_t)~mask;
  if (level == 0) S.pin0[p] |= (uint8_t)mask; else if (level == 1) S.pin1[p] |= (uint8_t)mask;
  if (p == PA && (port_read(PA) & mask)) S.wakeupreq |= 0x01;
}
/* ---- HT24LC08 (peripherals/HT24LC08.py), SDA = PD bit 1, SCL = PD bit 0, address 0xA0 ---- */
enum { EE_IDLE, EE_ADDR, EE_WORD, EE_WRITE, EE_READ };
static void ee_port(int value)
{
  int prev_scl = S.ee_scl, prev_sda = S.ee_sda;
  S.ee_scl = (value & 1) ? 1 : 0;
  S.ee_sda = (value & 2) ? 1 : 0;
  if (S.ee_scl && (prev_scl != S.ee_scl || prev_sda != S.ee_sda)) {
    if (prev_sda > S.ee_sda) { S.ee_state = EE_ADDR; S.ee_bits = 0; S.ee_ack = 0; cpu_port_in(PD, 2, -1); }
    else if (prev_sda < S.ee_sda) { S.ee_state = EE_IDLE; S.ee_ack = 0; }
    else if (S.ee_ack) { cpu_port_in(PD, 2, 0); S.ee_ack = 0; }
    else if (S.ee_state == EE_READ) {
      if (S.ee_bits < 8) { cpu_port_in(PD, 2, ((S.ee_tx >> (7 - S.ee_bits)) & 1) ? -1 : 0); S.ee_bits++; }
      else { S.ee_addr = (uint16_t)((S.ee_addr + 1) % V3_EE_SIZE); S.ee_tx = S.ee[S.ee_addr]; S.ee_bits = 0; }
    } else {
      cpu_port_in(PD, 2, -1);
      S.ee_rx = (uint8_t)((S.ee_rx << 1) | (S.ee_sda > 0));
      S.ee_bits++;
      if (S.ee_bits == 8) {
        int rx = S.ee_rx; S.ee_bits = 0;
        if (S.ee_state == EE_ADDR) {
          if ((rx & 0xF8) == 0xA0) {
            S.ee_ack = 1;
            S.ee_addr = (uint16_t)(((rx & ~0xF8) >> 1) * 256 + (S.ee_addr % 256));
            if (rx & 1) { S.ee_state = EE_READ; S.ee_tx = S.ee[S.ee_addr]; } else S.ee_state = EE_WORD;
          } else S.ee_state = EE_IDLE;
        } else if (S.ee_state == EE_WORD) {
          S.ee_ack = 1; S.ee_addr = (uint16_t)(S.ee_addr - (S.ee_addr % 256) + rx); S.ee_state = EE_WRITE;
        } else if (S.ee_state == EE_WRITE) {
          S.ee_ack = 1;
          if (S.ee[S.ee_addr] != rx) { S.ee[S.ee_addr] = (uint8_t)rx; S.ee_dirty = 1; }
          S.ee_addr = (uint16_t)(S.ee_addr - (S.ee_addr % 16) + ((S.ee_addr + 1) % 16));
        }
      }
    }
  }
}
/* ---- infrared (CON_V3_IR.py): receiver PB bit 2, sender PC bit 2; constants in cycles, here in ticks ---- */
#define IR_T(c) ((int32_t)(c) * (int32_t)V3_TICKS)
#define IR_AGC_B IR_T(38232)
#define IR_AGC_S IR_T(9908)
#define IR_BIT_B IR_T(1768)
#define IR_S0 IR_T(3088)
#define IR_S1 IR_T(5508)
#define IR_END IR_T(4680)
#define IR_PULSE_TH IR_T(104 + 5)
#define IR_BIT_TH IR_T((5508 + 3088) / 2)
enum { IR_IDLE, IR_SEND, IR_STOP };
static void ir_port(int value)          /* port_handler: the CPU wrote port C */
{
  if (S.ir_prev == (uint8_t)value) return;
  S.ir_prev = (uint8_t)value;
  if (!(value & 4)) return;
  if (S.ir_txs < IR_PULSE_TH) S.ir_txb += S.ir_txs;
  else {
    if (S.ir_txb < IR_END) {
      S.ir_txbyte >>= 1;
      if (S.ir_txs > IR_BIT_TH) S.ir_txbyte |= 0x80;
      if (++S.ir_txbits == 8) { if (S.ir_txn < sizeof S.ir_txq) S.ir_txq[S.ir_txn++] = S.ir_txbyte; S.ir_txbits = 0; }
    } else S.ir_txbits = 0;
    S.ir_txb = 0;
  }
  S.ir_txs = 0;
}
static void cpu_port_in(int p, int mask, int level);
static void ir_rx_clock(int32_t t);
static inline void ir_clock(int32_t t)  /* clock(cycles) */
{
  if (S.ir_txs < 0x40000000) S.ir_txs += t;
  if (S.ir_rxstep != IR_IDLE) ir_rx_clock(t);
}
static void ir_rx_clock(int32_t t)
{
  S.ir_rxb += t;
  if (S.ir_rxb < 0) return;
  cpu_port_in(PB, 4, 1);
  S.ir_rxs += t;
  if (S.ir_rxstep == IR_STOP) { S.ir_rxstep = IR_IDLE; return; }
  if (S.ir_rxs < 0) return;
  cpu_port_in(PB, 4, 0);
  if (S.ir_rxbits == 8) {
    S.ir_rxbits = 0;
    if (S.ir_rxn) { S.ir_rxbyte = S.ir_rxq[0]; S.ir_rxn--; memmove(S.ir_rxq, S.ir_rxq + 1, S.ir_rxn); }
    else { S.ir_rxb = -IR_END; S.ir_rxstep = IR_STOP; return; }
  }
  S.ir_rxb = -IR_BIT_B;
  S.ir_rxs = (S.ir_rxbyte & 1) ? -IR_S1 : -IR_S0;
  S.ir_rxbyte >>= 1; S.ir_rxbits++;
}
void v3_ir_rx(const uint8_t *d, int n)   /* serial_rx_handler: bytes from the other toy */
{
  for (int i = 0; i < n && S.ir_rxn < sizeof S.ir_rxq; i++) S.ir_rxq[S.ir_rxn++] = d[i];
  if (S.ir_rxstep == IR_IDLE && S.ir_rxn) {
    cpu_port_in(PB, 4, 0);
    S.ir_rxbyte = S.ir_rxq[0]; S.ir_rxn--; memmove(S.ir_rxq, S.ir_rxq + 1, S.ir_rxn);
    S.ir_rxbits = 0; S.ir_rxb = -IR_AGC_B; S.ir_rxs = -IR_AGC_S; S.ir_rxstep = IR_SEND;
  }
}
int v3_ir_tx(uint8_t *out, int max)       /* emit_serial_tx: bytes the toy sent */
{
  int n = S.ir_txn < max ? S.ir_txn : max;
  for (int i = 0; i < n; i++) out[i] = S.ir_txq[i];
  S.ir_txn = (uint8_t)(S.ir_txn - n); memmove(S.ir_txq, S.ir_txq + n, S.ir_txn);
  return n;
}
static void emit_port(int p) { if (p == PD) ee_port(port_read(PD)); else if (p == PCP) ir_port(port_read(PCP)); }

/* ---- flags ---- */
static int get_ps(void) { return (NF << 7) | (VF << 6) | (BF << 4) | (DF << 3) | (IF << 2) | (ZF << 1) | CF; }
static void set_ps(int ps) { NF = ps >> 7 & 1; VF = (ps & 0x40) > 0; BF = (ps & 0x10) > 0; DF = (ps & 0x08) > 0; IF = (ps & 0x04) > 0; ZF = (ps & 0x02) > 0; CF = ps & 1; }

static void tm_sync(void);
/* ---- SFRs ---- */
static int sfr_read(int a)
{
#ifdef V3_TMFAST
  if (a == 0x0C || (a >= 0x10 && a <= 0x15) || a == 0x01 || a == 0x18 || a == 0x38) tm_sync();
#else
  if (a == 0x0C || (a >= 0x10 && a <= 0x15)) tm_sync();
#endif
  switch (a) {
  case 0x00: return (int)(S.rom_bank >> 15);
  case 0x04: return S.cpu_div;
  case 0x08: return port_read(PA);
  case 0x0A: return port_read(PB);
  case 0x0B: return port_read(PCP);
  case 0x09: return port_read(PD);
  case 0x24: return S.lcd_ctrl1;
  case 0x01: return S.ireq;
  case 0x38: return S.nmireq | (S.nmi_ctrl & 0x80);
  case 0x18: return S.wakeupreq;
  case 0x0C: return (int)S.t1khz & 0xFF;     /* Python returns T1KHZ unmasked; a byte read keeps the low 8 bits */
  case 0x0F: return S.timer_ctrl;
  case 0x10: return (int)(S.tm0 >> 7) & 0xFF;
  case 0x11: return (int)(S.tm0 >> 7) >> 8 & 0xFF;
  case 0x14: return (int)(S.tm1 >> 7) & 0xFF;
  case 0x15: return (int)(S.tm1 >> 7) >> 8 & 0xFF;
  case 0x3E: return S.byte_mirror;
  case 0x3F: return S.nibble_swap;
  case 0x12: return S.a0c;
  case 0x16: return S.a1c;
  case 0x17: return S.a1d;
  default: return 0;
  }
}
static int bexp(int v) { return ((v & 1) * 3) | ((v & 2) * 6) | ((v & 4) * 0xC) | ((v & 8) * 0x18); }
static void snd_data(int ch, int ctrl, int data)
{
  if (ctrl & 0x40) S.snd_env[ch] = (uint8_t)data;
  else S.snd_env[ch] = 0;   /* PCM samples: not on the buzzer */
}
static void sfr_write(int a, int v)
{
  v &= 0xFF;
  int sy = a == 0x0C || a == 0x0F || (a >= 0x10 && a <= 0x15) || a == 0x38;
#ifdef V3_TMFAST
  sy |= a == 0x01 || a == 0x02 || a == 0x18 || a == 0x12 || a == 0x16 || a == 0x13 || a == 0x17;
#endif
  if (sy) tm_sync();
  switch (a) {
  case 0x00: S.rom_bank = (uint32_t)(v & 0x0F) << 15; break;
  case 0x04: if ((v & 7) == 7) S.cpu_enbl = 0; else S.cpu_div = (uint8_t)(v & 7); break;
  case 0x0E: S.clk32k = (uint8_t)(v & 0x80); break;
  case 0x06: S.attr[PA] = (uint8_t)v; emit_port(PA); break;
  case 0x07: S.pdir[PA] = (uint8_t)v; emit_port(PA); break;
  case 0x08: S.platch[PA] = (uint8_t)v; emit_port(PA); break;
  case 0x28: S.attr[PB] = (uint8_t)bexp(v); emit_port(PB); break;
  case 0x29: S.pdir[PB] = (uint8_t)bexp(v); emit_port(PB); break;
  case 0x0A: S.platch[PB] = (uint8_t)v; emit_port(PB); break;
  case 0x2A: S.attr[PCP] = (uint8_t)v; emit_port(PCP); break;
  case 0x2B: S.pdir[PCP] = (uint8_t)v; emit_port(PCP); break;
  case 0x0B: S.platch[PCP] = (uint8_t)v; emit_port(PCP); break;
  case 0x2C: S.attr[PD] = (uint8_t)(v & 0x3F); emit_port(PD); break;
  case 0x2D: S.pdir[PD] = (uint8_t)(v & 0x3F); emit_port(PD); break;
  case 0x09: S.platch[PD] = (uint8_t)(v & 0x3F); emit_port(PD); break;
  case 0x24: S.lcd_ctrl1 = (uint8_t)v; break;
  case 0x01: S.int_ctrl = (uint8_t)v; S.ireq = 0; break;
  case 0x02: S.ireq &= (uint8_t)~v; break;
  case 0x38: S.nmi_ctrl = (uint8_t)v; S.nmireq &= (uint8_t)v; break;
  case 0x18: S.wakeup_ctrl = (uint8_t)v; S.wakeupreq &= (uint8_t)v; break;
  case 0x0C: S.time_base = (uint8_t)v; break;
  case 0x0F: S.timer_ctrl = (uint8_t)v; break;
  case 0x10: S.tm0_rel = (S.tm0_rel & 0xFF00u) | (uint32_t)v; break;
  case 0x11: S.tm0_rel = (S.tm0_rel & 0xFFu) | ((uint32_t)v << 8); S.tm0 = (int64_t)S.tm0_rel << 7; break;
  case 0x14: S.tm1_rel = (S.tm1_rel & 0xFF00u) | (uint32_t)v; break;
  case 0x15: S.tm1_rel = (S.tm1_rel & 0xFFu) | ((uint32_t)v << 8); S.tm1 = (int64_t)S.tm1_rel << 7; break;
  case 0x3E: { int b = (v & 0xF0) >> 4 | (v & 0x0F) << 4; b = (b & 0xCC) >> 2 | (b & 0x33) << 2; S.byte_mirror = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1); } break;
  case 0x3F: S.nibble_swap = (uint8_t)(((v & 0x0F) << 4) | ((v & 0xF0) >> 4)); break;
  case 0x12: S.a0c = (uint8_t)v; break;
  case 0x13: S.a0d = (uint8_t)v; snd_data(0, S.a0c, v); break;
  case 0x16: S.a1c = (uint8_t)v; break;
  case 0x17: S.a1d = (uint8_t)v; snd_data(1, S.a1c, v); break;
  default: break;
  }
  if (sy) tm_sync();   /* the next event after the change */
}
/* ---- memory ---- */
static inline int RD(int addr)
{
  if (addr >= 0xC000) return rom_byte((uint32_t)addr & 0x7FFF);
  if (addr >= 0x4000) return rom_byte(((uint32_t)addr & 0x7FFF) | S.rom_bank);
  if (addr >= DP_OFF) return addr - DP_OFF < (int)V3_DPRAM ? S.dpram[addr - DP_OFF] : 0;
  if (addr >= RAM_OFF && addr < RAM_OFF + RAM_SIZE) return S.ram[addr - RAM_OFF];
  return sfr_read(addr);
}
static inline void WR(int addr, int v)
{
  if (addr >= RAM_OFF && addr < RAM_OFF + RAM_SIZE) S.ram[addr - RAM_OFF] = (uint8_t)v;
  else if (addr >= DP_OFF && addr < DP_OFF + (int)V3_DPRAM) S.dpram[addr - DP_OFF] = (uint8_t)v;
  else sfr_write(addr, v);
}
static void irq_enter(void)
{
  WR(SP | 0x100, PC >> 8); SP = (SP - 1) & 0xFF;
  WR(SP | 0x100, PC & 0xFF); SP = (SP - 1) & 0xFF;
  WR(SP | 0x100, get_ps()); SP = (SP - 1) & 0xFF;
  IF = 1; PC = S.addr_irq;
}
static void nmi_enter(void)
{
  if ((S.nmi_ctrl & 0x80) && S.cpu_enbl) {
    WR(SP | 0x100, PC >> 8); SP = (SP - 1) & 0xFF;
    WR(SP | 0x100, PC & 0xFF); SP = (SP - 1) & 0xFF;
    WR(SP | 0x100, get_ps()); SP = (SP - 1) & 0xFF;
    PC = S.addr_nmi;
  }
}
static void do_adc(int operand)
{
  int a = A, nv = a + operand + CF;
  if (DF && ((a & 0x0F) + (operand & 0x0F) + CF > 9)) nv += 6;
  VF = ((~(a ^ operand) & (a ^ nv)) >> 7) & 1;
  NF = (nv >> 7) & 1;
  if (DF && nv > 0x99) nv += 0x60;
  ZF = !(nv & 0xFF); CF = nv > 255; A = nv & 0xFF;
}
static void do_sbc(int operand)
{
  int a = A, nv = a - operand - (!CF);
  if (DF) { if ((a & 0x0F) - (operand & 0x0F) - (!CF) < 0) nv -= 6; if (nv < 0) nv -= 0x60; }
  VF = (((a ^ operand) & (a ^ nv)) >> 7) & 1;
  NF = (nv >> 7) & 1; ZF = !(nv & 0xFF); CF = nv >= 0; A = nv & 0xFF;
}
static int op_dummy(void) { return 2; }
#include "v3ops.inc"

/* ---- timers (SPLB32._timers_clock) ---- */
static const uint16_t TB_H[4] = { 0x07, 0x01, 0x03, 0x00 }, TB_L[4] = { 0x1FF, 0x7F, 0xFF, 0x3F };
static void timers(int64_t t)
{
  if (S.timer_ctrl & 0x60) {
    S.tm0_cnt -= t;
    while (S.tm0_cnt <= 0) {
      int m = S.timer_ctrl & 0x60;
      if (m == 0x20) { S.tm0_cnt = V3_TICKS; S.tm0 += t; }
      else if (m == 0x40) { S.tm0_cnt += SUB_DIV; S.tm0 += V3_TICKS; }
      else { S.tm0_cnt = V3_TICKS; break; }   /* external clock: not done (the Python would hang) */
      while (S.tm0 > ((int64_t)0xFFFF << 7)) {
        S.tm0 -= (int64_t)(0x10000 - S.tm0_rel) << 7;
        S.ireq |= 0x10; S.wakeupreq |= 0x04;
        if ((S.a0c & 0x08) && (S.a0c & 0x40) && S.snd_env[0]) S.snd_tog[0] ^= 1;
        if ((S.a1c & 0x08) && (S.a1c & 0x40) && S.snd_env[1]) S.snd_tog[1] ^= 1;
      }
    }
  }
  if (S.timer_ctrl & 0x03) {
    S.tm1_cnt -= t;
    while (S.tm1_cnt <= 0) {
      int m = S.timer_ctrl & 0x03;
      if (m == 0x01) { S.tm1_cnt = V3_TICKS; S.tm1 += t; }
      else if (m == 0x02) { S.tm1_cnt += SUB_DIV; S.tm1 += V3_TICKS; }
      else { S.tm1_cnt = V3_TICKS; break; }
      while (S.tm1 > ((int64_t)0xFFFF << 7)) {
        S.tm1 -= (int64_t)(0x10000 - S.tm1_rel) << 7;
        S.ireq |= 0x20; S.wakeupreq |= 0x10;
        S.nmireq |= 0x02;
        if (S.nmi_ctrl & 0x02) nmi_enter();
        if ((S.a0c & 0x10) && (S.a0c & 0x40) && S.snd_env[0]) S.snd_tog[0] ^= 1;
        if ((S.a1c & 0x10) && (S.a1c & 0x40) && S.snd_env[1]) S.snd_tog[1] ^= 1;
      }
    }
  }
  S.t1k_cnt -= t;
  while (S.t1k_cnt <= 0) {
    S.t1k_cnt += T1K_DIV;
    S.t1khz++;
    if (!(S.t1khz & TB_H[S.time_base & 3])) { S.ireq |= 0x08; S.wakeupreq |= 0x08; }
    if (!(S.t1khz & TB_L[(S.time_base & 0x0C) >> 2])) { S.ireq |= 0x04; S.wakeupreq |= 0x02; }
  }
}
/* 1.6.9: the timers are given the ticks only when something can happen (an overflow, the 1 kHz step) or when the
 * program reads or sets them; in between nothing of them is seen, so the run is the same as tick by tick */
static void tm_next(void)
{
  uint64_t n = 0xFFFFFFFFu; const int64_t XM = (int64_t)0xFFFF << 7;
  int m0 = S.timer_ctrl & 0x60, m1 = S.timer_ctrl & 0x03;
  if (m0 == 0x20) { int64_t d = XM - S.tm0 + 1; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint64_t)d; }
  else if (m0 == 0x40) { int64_t k = (XM - S.tm0) >= 0 ? ((XM - S.tm0) >> 7) + 1 : 1, d = S.tm0_cnt + (k - 1) * SUB_DIV; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint64_t)d; }
  if (m1 == 0x01) { int64_t d = XM - S.tm1 + 1; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint64_t)d; }
  else if (m1 == 0x02) { int64_t k = (XM - S.tm1) >= 0 ? ((XM - S.tm1) >> 7) + 1 : 1, d = S.tm1_cnt + (k - 1) * SUB_DIV; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint64_t)d; }
  { int64_t d = S.t1k_cnt; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint64_t)d; }
  S.next = (uint32_t)n;
}
#ifdef V3_TMFAST
/* 1.6.13 test12: the same timers without a pass per 32 kHz step and without 64-bit arithmetic where the values fit in
 * 32 bits (the counters stay int64 in the state, so saves keep their layout). The overflows of a counter are handled
 * after all its steps instead of between them: nothing else is read in between, so the result is the same. Out of
 * the usual range: the code above. */
#define TM_OK(v) ((v) > -0x40000000LL && (v) < 0x40000000LL)
static int tm_step32(int64_t *cnt, int64_t *tm, int m, int32_t t, int slow)   /* 1: done, 0: take the code above */
{
  if (!TM_OK(*cnt) || !TM_OK(*tm)) return 0;
  int32_t c = (int32_t)*cnt - t;
  if (c > 0) { *cnt = c; return 1; }
  if (!slow) { *cnt = V3_TICKS; *tm += t; return 1; }              /* main clock: one pass */
  uint32_t n = (uint32_t)(-c) / SUB_DIV + 1u;
  *cnt = (int64_t)c + (int64_t)n * SUB_DIV; *tm += (int64_t)n * V3_TICKS;
  (void)m; return 1;
}
static void timers_fast(uint32_t t)
{
  if (t >= 0x40000000u) { timers(t); return; }
  int m0 = S.timer_ctrl & 0x60, m1 = S.timer_ctrl & 0x03;
  if (m0) {
    if (m0 == 0x60 || !TM_OK(S.tm0_cnt) || !TM_OK(S.tm0)) goto slow;
  }
  if (m1) {
    if (m1 == 0x03 || !TM_OK(S.tm1_cnt) || !TM_OK(S.tm1)) goto slow;
  }
  if (!TM_OK(S.t1k_cnt)) goto slow;
  if (m0) {
    tm_step32(&S.tm0_cnt, &S.tm0, m0, (int32_t)t, m0 == 0x40);
    while (S.tm0 > ((int64_t)0xFFFF << 7)) {
      S.tm0 -= (int64_t)(0x10000 - S.tm0_rel) << 7;
      S.ireq |= 0x10; S.wakeupreq |= 0x04;
      if ((S.a0c & 0x08) && (S.a0c & 0x40) && S.snd_env[0]) S.snd_tog[0] ^= 1;
      if ((S.a1c & 0x08) && (S.a1c & 0x40) && S.snd_env[1]) S.snd_tog[1] ^= 1;
    }
  }
  if (m1) {
    tm_step32(&S.tm1_cnt, &S.tm1, m1, (int32_t)t, m1 == 0x02);
    while (S.tm1 > ((int64_t)0xFFFF << 7)) {
      S.tm1 -= (int64_t)(0x10000 - S.tm1_rel) << 7;
      S.ireq |= 0x20; S.wakeupreq |= 0x10;
      S.nmireq |= 0x02;
      if (S.nmi_ctrl & 0x02) nmi_enter();
      if ((S.a0c & 0x10) && (S.a0c & 0x40) && S.snd_env[0]) S.snd_tog[0] ^= 1;
      if ((S.a1c & 0x10) && (S.a1c & 0x40) && S.snd_env[1]) S.snd_tog[1] ^= 1;
    }
  }
  { int32_t c = (int32_t)S.t1k_cnt - (int32_t)t;
    if (c <= 0) {
      uint32_t n = (uint32_t)(-c) / T1K_DIV + 1u, o = S.t1khz, h = TB_H[S.time_base & 3], l = TB_L[(S.time_base & 0x0C) >> 2];
      c += (int32_t)(n * T1K_DIV); S.t1khz = o + n;
      /* a step onto a multiple of h + 1 (h = 2^k - 1): the bits above h changed (n < 2^31) */
      if (!h || ((o + n) & ~h) != (o & ~h) || n > h) { S.ireq |= 0x08; S.wakeupreq |= 0x08; }
      if (!l || ((o + n) & ~l) != (o & ~l) || n > l) { S.ireq |= 0x04; S.wakeupreq |= 0x02; }
    }
    S.t1k_cnt = c; }
  return;
slow:
  timers(t);
}
static void tm_next_fast(void)
{
  const int32_t XM = 0xFFFF << 7;
  if (!TM_OK(S.tm0) || !TM_OK(S.tm1) || !TM_OK(S.tm0_cnt) || !TM_OK(S.tm1_cnt) || !TM_OK(S.t1k_cnt)) { tm_next(); return; }
  uint32_t n = 0x10000000u; int32_t tm0 = (int32_t)S.tm0, tm1 = (int32_t)S.tm1;
  int m0 = S.timer_ctrl & 0x60, m1 = S.timer_ctrl & 0x03;
  /* an event nobody would see before the next sync (its flags masked, no NMI) is left to that sync: the flags then
   * read the same (the flag registers sync when read or written) */
  if (!(S.int_ctrl & 0x10) && !(S.wakeup_ctrl & 0x04)) m0 = 0;
  if (!(S.int_ctrl & 0x20) && !(S.wakeup_ctrl & 0x10) && !(S.nmi_ctrl & 0x02)) m1 = 0;
  if (m0 == 0x20) { int32_t d = XM - tm0 + 1; if (d < 1) d = 1; if ((uint32_t)d < n) n = (uint32_t)d; }
  else if (m0 == 0x40) { int32_t k = (XM - tm0) >= 0 ? ((XM - tm0) >> 7) + 1 : 1; int64_t d = S.tm0_cnt + (int64_t)(k - 1) * SUB_DIV; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint32_t)d; }
  if (m1 == 0x01) { int32_t d = XM - tm1 + 1; if (d < 1) d = 1; if ((uint32_t)d < n) n = (uint32_t)d; }
  else if (m1 == 0x02) { int32_t k = (XM - tm1) >= 0 ? ((XM - tm1) >> 7) + 1 : 1; int64_t d = S.tm1_cnt + (int64_t)(k - 1) * SUB_DIV; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint32_t)d; }
  { uint32_t o = S.t1khz, j = 0xFFFFFFFFu, h = TB_H[S.time_base & 3], l = TB_L[(S.time_base & 0x0C) >> 2];
    if ((S.int_ctrl & 0x08) || (S.wakeup_ctrl & 0x08)) j = ((o | h) + 1u) - o;          /* steps to the next flag */
    if ((S.int_ctrl & 0x04) || (S.wakeup_ctrl & 0x02)) { uint32_t k = ((o | l) + 1u) - o; if (k < j) j = k; }
    if (j > 0x1000u) j = 0x1000u;
    int64_t d = S.t1k_cnt + (int64_t)(j - 1u) * T1K_DIV; if (d < 1) d = 1; if ((uint64_t)d < n) n = (uint32_t)d; }
  S.next = n;
}
static void tm_sync(void) { if (S.pend) { timers_fast(S.pend); S.pend = 0; } tm_next_fast(); }
#else
static void tm_sync(void) { if (S.pend) { timers(S.pend); S.pend = 0; } tm_next(); }
#endif
uint32_t v3_step(void)
{
  if (S.cpu_enbl) {
    int op = RD(PC); PC = (PC + 1) & 0xFFFF;
    uint32_t c = (uint32_t)v3_op[op]() << S.cpu_div, t = c * V3_TICKS;
    if ((S.pend += t) >= S.next) tm_sync();
    if (!IF && (S.ireq & S.int_ctrl)) irq_enter();
    ir_clock((int32_t)t);
    return t;
  }
  if (S.wakeup_ctrl & S.wakeupreq) { S.cpu_enbl = 1; S.cpu_div = 2; }
  if (S.clk32k && (S.pend += SUB_DIV) >= S.next) tm_sync();
  ir_clock(SUB_DIV);
  return SUB_DIV;
}
/* runs at least ticks (the rest of the last call counted in); while stopped, the 32 kHz steps without an event at once */
#ifndef V3_BATCH
#define V3_BATCH 1
#endif
static int64_t g_debt;
uint64_t v3_run(uint64_t ticks)
{
#ifdef V3_TMFAST
  /* test12: the usual call (1 ms) in 32-bit arithmetic, the same steps */
  if (ticks < 0x20000000u && g_debt < 0x20000000 && g_debt > -0x20000000) {
    int32_t d = 0, want = (int32_t)ticks - (int32_t)g_debt;
    while (d < want) {
      if (V3_BATCH && !S.cpu_enbl && !(S.wakeup_ctrl & S.wakeupreq) && S.ir_rxstep == IR_IDLE) {
        uint32_t k = (uint32_t)(want - d) / SUB_DIV;
        if (S.clk32k) { uint32_t e = (S.next - S.pend - 1u) / SUB_DIV; if (e < k) k = e; }
        if (k > 1) { k--; if (S.clk32k) S.pend += k * SUB_DIV; d += (int32_t)(k * SUB_DIV); if (S.ir_txs < 0x40000000) S.ir_txs += (int32_t)(k < 0x8000u ? k * SUB_DIV : 0x40000000u); continue; }
      }
      d += (int32_t)v3_step();
    }
    g_debt = d - want;
    return (uint64_t)d;
  }
#endif
  uint64_t d = 0; int64_t want = (int64_t)ticks - g_debt;
  while ((int64_t)d < want) {
    if (V3_BATCH && !S.cpu_enbl && !(S.wakeup_ctrl & S.wakeupreq) && S.ir_rxstep == IR_IDLE) {
      int64_t lf = want - (int64_t)d; uint32_t k = lf > 0x7FFFFFFF ? 0x7FFFFFFFu / SUB_DIV : (uint32_t)lf / SUB_DIV;
      if (S.clk32k) { uint32_t e = (S.next - S.pend - 1u) / SUB_DIV; if (e < k) k = e; }   /* steps before the event */
      if (k > 1) { k--; if (S.clk32k) S.pend += k * SUB_DIV; d += (uint64_t)k * SUB_DIV; if (S.ir_txs < 0x40000000) S.ir_txs += (int32_t)(k < 0x8000u ? k * SUB_DIV : 0x40000000u); continue; }
    }
    d += v3_step();
  }
  g_debt = (int64_t)d - want;
  return d;
}
void v3_reset(void)
{
  uint8_t ee[V3_EE_SIZE]; memcpy(ee, S.ee, sizeof ee);
  memset(&S, 0, sizeof S); memcpy(S.ee, ee, sizeof ee);
  A = X = Y = SP = PC = 0; set_ps(0x04);
  S.cpu_enbl = 1; S.cpu_div = 2; S.clk32k = 0x80;
  S.addr_irq = (uint16_t)(rom_byte(VADDR_IRQ) | rom_byte(VADDR_IRQ + 1) << 8);
  S.addr_nmi = (uint16_t)(rom_byte(VADDR_NMI) | rom_byte(VADDR_NMI + 1) << 8);
  PC = rom_byte(VADDR_RESET) | rom_byte(VADDR_RESET + 1) << 8;
  S.ee_scl = 1; S.ee_sda = 1;
  tm_next(); g_debt = 0;
  cpu_port_in(PB, 4, 0);   /* the IR receiver (CON_V3_IR) holds PB bit 2 low */
}
void v3_init(const uint8_t *rom, uint32_t size) { g_rom = rom; g_rom_size = size; g_rom_mask = (size & (size - 1u)) ? 0u : size - 1u; memset(S.ee, 0, sizeof S.ee); v3_reset(); }
void v3_set_keys(uint32_t mask)
{
  static const uint8_t K[3] = { 1, 2, 4 };
  for (int i = 0; i < 3; i++) {
    int down = (mask >> i) & 1, was = (S.pin1[PA] & K[i]) != 0;
    if (down != was) cpu_port_in(PA, K[i], down ? 1 : -1);
  }
}
const uint8_t *v3_vram(int *mode)
{
  int m = 0;
  if (S.clk32k) { if ((S.lcd_ctrl1 & 0x0D) == 0x01) m = 1; else if (S.lcd_ctrl1 & 0x04) m = 2; }
  *mode = m; return S.dpram;
}
uint32_t v3_tone_hz(void)
{
  for (int ch = 0; ch < 2; ch++) {
    int c = ch ? S.a1c : S.a0c;
    if (!(c & 0x40) || !S.snd_env[ch]) continue;
    if (c & 0x08) { uint32_t n = 0x10000u - S.tm0_rel, m = S.timer_ctrl & 0x60; if (n && m == 0x20) return V3_CLOCK / n / 2u; if (n && m == 0x40) return 32768u / n / 2u; }
    if (c & 0x10) { uint32_t n = 0x10000u - S.tm1_rel, m = S.timer_ctrl & 0x03; if (n && m == 0x01) return V3_CLOCK / n / 2u; if (n && m == 0x02) return 32768u / n / 2u; }
  }
  return 0;
}
uint8_t *v3_eeprom(void) { return S.ee; }
/* test21: the EEPROM inside a saved state (v3_state_save), for a broken settings sector */
const uint8_t *v3_state_ee(const uint8_t *b) { return b + 56 + __builtin_offsetof(V3S, ee); }
int v3_eeprom_dirty(int clear) { int d = S.ee_dirty; if (clear) S.ee_dirty = 0; return d; }
uint8_t *v3_ram(void) { return S.ram; }
#define V3_SV_MAGIC 0x31535633u   /* "3VS1" */
uint32_t v3_state_size(void) { return 4u + 13u * 4u + (uint32_t)sizeof(V3S); }
void v3_state_save(uint8_t *b)
{
#if defined(V3_TMFAST) || defined(V3_SAVESYNC)
  tm_sync();                            /* the deferred timer events in (the same state, written out) */
#endif
  uint32_t r[14] = { V3_SV_MAGIC, (uint32_t)A, (uint32_t)X, (uint32_t)Y, (uint32_t)SP, (uint32_t)PC, (uint32_t)NF, (uint32_t)VF, (uint32_t)BF, (uint32_t)DF, (uint32_t)IF, (uint32_t)ZF, (uint32_t)CF, 0 };
  memcpy(b, r, 56); memcpy(b + 56, &S, sizeof S);
}
int v3_state_load(const uint8_t *b, uint32_t n)
{
  uint32_t r[14]; if (n < v3_state_size()) return 0;
  memcpy(r, b, 56); if (r[0] != V3_SV_MAGIC) return 0;
  A = (int)r[1]; X = (int)r[2]; Y = (int)r[3]; SP = (int)r[4]; PC = (int)r[5]; NF = (int)r[6]; VF = (int)r[7]; BF = (int)r[8]; DF = (int)r[9]; IF = (int)r[10]; ZF = (int)r[11]; CF = (int)r[12];
  memcpy(&S, b + 56, sizeof S);
  return 1;
}
uint32_t v3_pc(void) { return (uint32_t)PC; }
uint32_t v3_bank(void) { return S.rom_bank >> 15; }
void v3_regs(uint8_t r[5], uint8_t f[6]) { r[0] = (uint8_t)A; r[1] = (uint8_t)X; r[2] = (uint8_t)Y; r[3] = (uint8_t)SP; r[4] = 0; f[0] = (uint8_t)NF; f[1] = (uint8_t)VF; f[2] = (uint8_t)DF; f[3] = (uint8_t)IF; f[4] = (uint8_t)ZF; f[5] = (uint8_t)CF; }
