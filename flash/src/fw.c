#include "io.h"
#ifndef ANZEIGE
#define ANZEIGE 1
#endif
#ifndef PIX_BURST
#define PIX_BURST 0   /* 1 = old block transfer (1.3.7) */
#endif
#ifndef HALTBILD
/* 1 = default: the panel stays on while reading/writing and the progress bar is
 *     updated live, like the original flash utility. Confirmed on the device
 *     (dump with persistent screen is byte-identical to the P-COM reference).
 * 0 = proven fallback: screen only while idle, the panel turns off during access. */
#define HALTBILD 1
#endif
/*
 * Custom flash app for the Tamagotchi Paradise (mameise).
 * Runs from PRAM 0, loaded through the dongle via PDLO. Speaks PAU1 over UART1,
 * so the existing Windows tool (dump, write back) works unchanged.
 *
 * Only constants (.rodata) and fixed SRAM addresses, no .data/.bss, so no
 * copy at load time is needed.
 */

/* ---- UART1 (0x4000B000) ---- */
#define U_DATA REG(UART+0x00)
#define U_IER  REG(UART+0x04)
#define U_FCR  REG(UART+0x08)
#define U_LCR  REG(UART+0x0C)
#define U_LSR  REG(UART+0x14)
#define U_FDR  REG(UART+0x28)
#define U_CTRL REG(UART+0x30)

/* ---- Flash controller (0x40022000) ---- */
#define F_STAT REG(FCTL+0x000)
#define F_CMD  REG(FCTL+0x004)
#define F_IDX  REG(FCTL+0x010)
#define F_DATA REG(FCTL+0x014)
#define F_JED  REG(FCTL+0x018)
#define F_DMEM REG(FCTL+0x100)
#define F_DLEN REG(FCTL+0x104)
#define F_DCTL REG(FCTL+0x108)
#define F_DFLA REG(FCTL+0x10C)

/* ---- fixed SRAM buffers (128 KB SRAM at 0x18000000, stack at 0x1801F000) ----
 * 1.8.0: blocks grown to 4 KB (MAXP), so the I/O buffers are now 8 KB apart.
 * A 4 KB payload frame is 28+4096+4 = 4128 bytes, slightly more when COBS
 * encoded, and fits in 8 KB with room to spare. The layout does not overlap and
 * the stack stays free (COBSO ends at 0x19FFF, the stack grows down from 0x1F000). */
#define DMABUF 0x18010000u   /* 4 KB, target/source of the flash DMA */
#define SECBUF 0x18011000u   /* 4 KB, sector staging for writes */
#define RXACC  0x18012000u   /* 8 KB, raw COBS buffer */
#define FRAME  0x18014000u   /* 8 KB, decoded frame */
#define OUTF   0x18016000u   /* 8 KB, reply frame */
#define COBSO  0x18018000u   /* 8 KB, COBS encoded reply */

#define FLASH_SIZE 0x1000000u
#ifndef MAXP
#define MAXP 4096u           /* Block size. 1024 = proven fallback. */
#endif

static u8* const dma  = (u8*)DMABUF;
static u8* const sec  = (u8*)SECBUF;
static u8* const rxac = (u8*)RXACC;
static u8* const frm  = (u8*)FRAME;
static u8* const outf = (u8*)OUTF;
static u8* const cob  = (u8*)COBSO;

/* ============================ Hardware init ============================ */
/* Faithful to the firmware (SystemInit, clock routine 0x8c38+0x8a58). XIP is
 * NOT needed: we run from PRAM and read the flash via DMA. */
static void clock_init(void){
  REG(CLK+0x00) |= 1u;
  REG(CLK+0x0C) = 0;
  REG(CLK+0x00) = 0x41u;
  { u32 r = (REG(CLK+0x34) & 0xF00u) | 0xA55A0000u; REG(CLK+0x04) = r; }
  REG(CLK+0x18) = 0x21u;
  REG(CLK+0x50) = 0;
  REG(CLK+0x10C) = 0x001FFFFDu;
  REG(CLK+0x110) = 0;
  REG(CLK+0x108) = 0;
  REG(CLK+0x10)  = 0;
  REG(CLK+0x00) |= 0x10u;
  while(!((REG(CLK+0x08) >> 4) & 1u)){}
  REG(CLK+0x00) |= 0x4000u;
  REG(CLK+0x04) &= ~1u;
  REG(CLK+0x00) |= 0x8000u;
  while(!((REG(CLK+0x08) >> 6) & 1u)){}
  { u32 r = (REG(CLK+0x0C) & ~7u) | 4u; REG(CLK+0x0C) = r; }
  while( ((REG(CLK+0x0C) >> 4) & 7u) != (REG(CLK+0x0C) & 7u) ){}
  REG(CLK+0x100) = 0x00157EB7u;
}
static void flashctl_init(void){
  /* 1.4.2: enable XIP. We read through the 0x60000000 window that the XIP block
   * maps in. Without XIP that window only shows the config register (0x40), not
   * the flash data. The original flash utility does this at startup, our app did
   * not do it after reset until now. That was exactly what was missing. */
  REG(0x4002F004u)=0x60011000u;
  REG(0x4002F000u)=0x00000003u;
  /* 1.4.1: controller init exactly like the original flash utility (0x40022000).
   * This includes setting the read command/mode in 0x008 and 0x00c (0x3077 / 0xf000),
   * otherwise the chip only returns its config register (0x40) instead of the flash data. */
  F_STAT=0x0000d018u;
  F_STAT=0x0000d000u;
  F_STAT=0x0000d080u;
  F_STAT=0x0000d088u;
  REG(FCTL+0x008)=0x00003077u;   /* read command/mode */
  REG(FCTL+0x00C)=0x0000f000u;
  REG(FCTL+0x004)=0x00000400u;
  REG(FCTL+0x004)=0x00004000u;
}
#if ANZEIGE
static void arm_for_dump(void);
#endif
static void uart_init(void){
  U_LCR=0x83; U_DATA=0x1A; U_IER=0x00; U_LCR=0x03; U_FDR=0x100; U_FCR=0x07; U_CTRL=0xC1;
}

/* ============================ UART input/output ============================ */
static void putb(u8 c){ while(!(U_LSR & 0x20u)){} U_DATA=c; }
static int  getb(void){
  u32 g=0;
  while(!(U_LSR & 0x01u)){
    if((++g & 0x3FFFFu)==0){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; } /* feed the watchdog */
  }
  return (int)(U_DATA & 0xFF);
}

/* ============================ Flash via DMA ============================ */
static void fctl_bracket(void){ /* like the firmware before each transfer */
  u32 r=F_STAT; F_STAT=r & ~0x10u; REG(FCTL+0x020)=0; F_IDX=0; F_STAT=r | 0x10u;
}
#define SPIN 2000000u
/* 1.4.0: read EXACTLY like the original flash utility (taken from its trace).
 * Two things we had wrong so far, hidden by the emulator (it masks the flash
 * address and reads directly, so it always read correctly):
 *  1. The flash address is given with the XIP base 0x60000000 (60xxxxxx),
 *     not bare. Without the base the real chip reads nothing -> all 0xFF.
 *  2. Status sequence at 0x40022000: d008 (read mode) -> 0x20=0, 0x10=0 -> d088,
 *     only then the DMA. */
/* 1.4.3: read through direct CPU access to the XIP window 0x60000000.
 * Tested against four methods on the device: only this one returns real data
 * (SONIXDEV), the controller DMA only gave the config register 0x40. Word by
 * word for speed. */
static void flash_read(u32 foff, u32 dst, u32 len){
  const volatile u32* s=(const volatile u32*)(0x60000000u + (foff & 0x00FFFFFFu));
  u32* d=(u32*)dst;
  u32 w=len>>2;
  for(u32 i=0;i<w;i++) d[i]=s[i];
  u32 rem=len & 3u;
  if(rem){ const volatile u8* sb=(const volatile u8*)(0x60000000u + (foff & 0x00FFFFFFu) + (w<<2));
           u8* db=(u8*)(dst + (w<<2)); for(u32 i=0;i<rem;i++) db[i]=sb[i]; }
}
/* SPI NOR helpers for writing (experimental, registers taken from the firmware). */
static void flash_wait_wip(void){
  /* Read the status register (reg08=0x05, like a read command, via the firmware
     routine); the controller handles waiting for WIP. */
  for(volatile int i=0;i<2000;i++){}
}
static void flash_write_enable(void){
  u32 r=F_STAT; F_STAT=r & ~0x10u; F_STAT=r | 0x10u;
}
static void flash_erase_4k(u32 foff){
  fctl_bracket();
  REG(FCTL+0x008)=0x306Au;   /* observed erase opcode constant */
  REG(FCTL+0x00C)=foff;
  flash_write_enable();
  F_STAT |= 0x400u;          /* start erase */
  for(u32 g=0; (F_STAT & 0x400u) && g<SPIN; g++){}
  flash_wait_wip();
}
/* Erase + program one 4 KB sector, exactly like the original flash utility.
 * Two steps: erase first (command without mode bit), then program (command
 * with mode bit 0x3000), address given as 0x60000000|addr.
 * Confirmed on the device against four read methods and with read/erase/write tests. */
static void prog_sector_fu(u32 addr, u32 src){
  u32 a1=addr>>1;
  u32 hi=(a1>>16)&0xFFFFu;   /* = addr>>17 */
  u32 lo=a1 & 0xFFFFu;       /* = (addr>>1)&0xFFFF */
  /* Step 1: ERASE the sector - command WITHOUT mode bit (like 0x38b0 in the FU). */
  REG(FCTL+0x008)=hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  for(u32 g=0; (REG(FCTL+0x004)&0x400u) && g<60000000u; g++){}
  for(volatile u32 i=0;i<2000000u;i++){}
  /* Step 2: PROGRAM - command WITH mode bit 0x3000, then DMA. */
  REG(FCTL+0x008)=0x3000u | hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  REG(FCTL+0x004)=0x00004000u;
  REG(FCTL+0x000)=0x0000d080u;
  REG(FCTL+0x020)=0;
  REG(FCTL+0x010)=0;
  REG(FCTL+0x000)=0x0000d082u;
  F_DFLA=0x60000000u | (addr & 0x00FFFFFFu);
  F_DLEN=0x1000u; F_DMEM=src;
  F_DCTL=0u;
  F_DCTL=1u;
  for(u32 g=0; (F_DCTL & 1u) && g<20000000u; g++){}
  for(volatile u32 i=0;i<2000000u;i++){}
}
static void flash_program(u32 foff, u32 src, u32 len){
  fctl_bracket();
  flash_write_enable();
  F_DFLA=0x60000000u | (foff & 0x00FFFFFFu); F_DLEN=len; F_DMEM=src;
  F_DCTL=0u;             /* direction memory -> flash */
  F_DCTL=1u;             /* Start */
  for(u32 g=0; (F_DCTL & 1u) && g<SPIN; g++){}
  flash_wait_wip();
}

/* ============================ CRC32 (bitwise) ============================ */
static u32 crc32_buf(u32 crc, const u8* p, u32 n){
  for(u32 i=0;i<n;i++){ crc ^= p[i];
    for(int k=0;k<8;k++) crc = (crc&1u) ? (0xEDB88320u ^ (crc>>1)) : (crc>>1); }
  return crc;
}
static u32 crc32_flash(u32 foff, u32 len){
  u32 crc=0xFFFFFFFFu;
  for(u32 o=0;o<len;o+=MAXP){ u32 n=(len-o<MAXP)?(len-o):MAXP;
    flash_read(foff+o, DMABUF, n); crc=crc32_buf(crc, dma, n); }
  return crc ^ 0xFFFFFFFFu;
}

/* ============================ COBS ============================ */
static u32 cobs_decode(const u8* s, u32 n, u8* o){
  u32 i=0, w=0;
  while(i<n){ u32 code=s[i++]; if(!code) continue;
    for(u32 k=1;k<code && i<n;k++) o[w++]=s[i++];
    if(code<0xFF && i<n) o[w++]=0; }
  return w;
}
static u32 cobs_encode(const u8* s, u32 n, u8* o){
  u32 w=0, ci=w++; u8 code=1;
  for(u32 i=0;i<n;i++){
    if(s[i]){ o[w++]=s[i]; if(++code==0xFF){ o[ci]=code; ci=w++; code=1; } }
    else { o[ci]=code; ci=w++; code=1; }
  }
  o[ci]=code; return w;
}

/* ============================ Display ============================ */
/*
 * Version 1.3.7: display rewritten after the code of the original flash utility
 * (no longer after the emulator trace). Fixes three bugs in 1.3.6:
 *  1. The backlight comes from a PWM timer (CT32B2, 0x40002000).
 *     1.3.6 never turned it on, so everything stayed dark.
 *  2. SPI control register 0x4000E000: the FU does |=0xC0 (clear FIFO), we
 *     wrote 0xC0 as an absolute value and so turned SPI off (bit0) and cleared
 *     the word width (0x700). The emulator reads the register as 0, so the
 *     trace looked like "=0xC0".
 *  3. GPIO0 bit5 is not a clock but D/C (command/data). Commands with
 *     D/C=0, parameters and pixels with D/C=1, switch only after the transfer
 *     has finished. Plus a real panel reset (P0.4) before init.
 * ANZEIGE=0 builds the app without display (dump only), like mameise-flashtool.pdlo.
 */
#include "assets.h"
#include "disp_power.h"
#define PANEL 0x4000E000u
static u16* const fb = (u16*)0x18008000u;   /* 128x128 framebuffer in SRAM */
static u32* const g_wrcnt=(u32*)0x1801A804u; /* sectors counted while writing (display) */

static void udelay(volatile u32 n){ while(n--) __asm__ volatile("nop"); }
#define MS(x) udelay((u32)(x)*16000u)        /* rough, rather too long than too short */

/* Bit-band like in the FU: write a single pin bit without touching the other
   pins of the port. */
#define BB(reg,bit) (*(volatile u32*)(0x42000000u + (((u32)(reg)-0x40000000u)<<5) + ((u32)(bit)<<2)))
#define GPIO0 0x40018000u
#define GPIO1 0x40019000u
#define LCD_RST(v) (BB(GPIO0+0x00,4)=(v))   /* P0.4 panel reset */
#define LCD_DC(v)  (BB(GPIO0+0x00,5)=(v))   /* P0.5 0=command 1=data */

static void display_power(void){
  /* Sequence from the original FU (clock enable, pin setup, timer CT32B3, GPIO1).
     As in the FU, 0x45000210..218 get the current contents of the GPIO data
     registers (the emulator had returned 0 there). */
  for(unsigned i=0;i<DISP_POWER_N;i++){
    u32 a=disp_power_seq[i][0], v=disp_power_seq[i][1];
    if(a>=0x45000210u && a<=0x45000218u) v=REG(0x4001803Cu + ((a-0x45000210u)>>2)*0x1000u);
    REG(a)=v;
  }
  /* Make sure the panel pins are outputs: reset, D/C, CS (P1.5 low), power (P1.9 low) */
  LCD_RST(1); BB(GPIO0+0x04,4)=1;
  LCD_DC(0);  BB(GPIO0+0x04,5)=1;
  BB(GPIO1+0x00,5)=0; BB(GPIO1+0x04,5)=1;
  BB(GPIO1+0x00,9)=0; BB(GPIO1+0x04,9)=1;
  MS(20);
}

#define SPI_CTRL REG(PANEL+0x00)
#define SPI_STAT REG(PANEL+0x0C)
#define SPI_DATA REG(PANEL+0x1C)
#define SPIWAIT 200000u
static void spi_init(void){
  /* like FU 0x3f44, as read-modify-write */
  REG(PANEL+0x08)=1;                                   /* clock divider */
  SPI_CTRL &= ~(1u<<18);
  REG(PANEL+0x04) &= ~6u; REG(PANEL+0x04) &= ~1u;
  SPI_CTRL &= ~0x10u; SPI_CTRL &= ~0x08u;
  REG(PANEL+0x20)=0;
  SPI_CTRL = (SPI_CTRL & ~0xF00u) | 0x700u;            /* 8 bit */
  REG(PANEL+0x24) |= 0x80000000u;
  REG(PANEL+0x24) &= ~0xFF0000u;
  REG(PANEL+0x24) |= 0x80000000u;
  REG(PANEL+0x24) &= ~0xFFu;
  SPI_CTRL |= 1u;                                      /* SPI on */
  SPI_CTRL |= 0xC0u;                                   /* clear FIFOs */
}
/* Send n bytes like FU 0x40e0: before each byte wait until TX is not full (bit1),
   at the end wait until no longer busy (bit4), then clear the FIFOs. */
static void spi_tx(const u8* p, u32 n){
  for(u32 i=0;i<n;i++){
    for(u32 g=0; (SPI_STAT & 2u) && g<SPIWAIT; g++){}
    SPI_DATA=p[i];
  }
  udelay(8);
  for(u32 g=0; (SPI_STAT & 0x10u) && g<SPIWAIT; g++){}
  SPI_CTRL |= 0xC0u;
}
static void lcd_cmd(u8 c, const u8* par, u32 n){
  LCD_DC(0); spi_tx(&c,1);
  if(n){ LCD_DC(1); spi_tx(par,n); LCD_DC(0); }
}
/* Panel init, byte for byte like the FU (from its trace with D/C). */
static const u8 p36[]={0x08}, p3a[]={0x05}, p26[]={0x01},
  pb1[]={0x08,0x3b,0x3b}, pb2[]={0x08,0x3b,0x3b}, pb3[]={0x08,0x3b,0x3b,0x08,0x3b,0x3b},
  pb4[]={0x03}, pc0[]={0xe0,0x00,0x84}, pc1[]={0xc4}, pc2[]={0x0d,0x00}, pc3[]={0x8d,0x2a},
  pc4[]={0x8d,0xee}, pc5[]={0x06},
  pe0[]={0x0d,0x17,0x16,0x28,0x3f,0x39,0x30,0x31,0x2c,0x28,0x2f,0x39,0x00,0x04,0x01,0x10},
  pe1[]={0x0a,0x15,0x18,0x25,0x3f,0x37,0x2e,0x30,0x2f,0x2c,0x33,0x3f,0x00,0x03,0x04,0x10},
  p35[]={0x00}, pcas0[]={0x00,0x02,0x00,0x83}, pras0[]={0x00,0x01,0x00,0x81},
  pcas[]={0x00,0x02,0x00,0x81}, pras[]={0x00,0x01,0x00,0x80};

static void display_init(void){
  spi_init();
  LCD_RST(1); MS(10); LCD_RST(0); MS(20); LCD_RST(1); MS(150);   /* panel reset */
  lcd_cmd(0x11,0,0); MS(150);                                    /* SLPOUT */
  lcd_cmd(0x36,p36,1); lcd_cmd(0x3a,p3a,1); lcd_cmd(0x26,p26,1); lcd_cmd(0x20,0,0);
  lcd_cmd(0xb1,pb1,3); lcd_cmd(0xb2,pb2,3); lcd_cmd(0xb3,pb3,6); lcd_cmd(0xb4,pb4,1);
  lcd_cmd(0xc0,pc0,3); lcd_cmd(0xc1,pc1,1); lcd_cmd(0xc2,pc2,2); lcd_cmd(0xc3,pc3,2);
  lcd_cmd(0xc4,pc4,2); lcd_cmd(0xc5,pc5,1);
  lcd_cmd(0xe0,pe0,16); lcd_cmd(0xe1,pe1,16);
  lcd_cmd(0x35,p35,1);
  lcd_cmd(0x29,0,0); MS(20);                                     /* DISPON */
  lcd_cmd(0x2a,pcas0,4); lcd_cmd(0x2b,pras0,4);
  lcd_cmd(0x11,0,0); MS(150);
}
/* Backlight: PWM on timer CT32B2, values like the FU (MR3=period,
   MR1=duty cycle, 0 = full brightness). */
#define CT2 0x40002000u
static void pwm2(u32 mr1, int first){
  if(first) REG(CT2+0x34)=0;
  REG(CT2+0x00)=0;
  REG(CT2+0x18)=0; REG(CT2+0x1C)=0; REG(CT2+0x20)=0; REG(CT2+0x24)=0;
  REG(CT2+0x14)=0;
  REG(CT2+0x00)=2; REG(CT2+0x08)=0; REG(CT2+0x00)=0;
  REG(CT2+0x14)=0x400u;
  REG(CT2+0x24)=0x17700u;
  REG(CT2+0x1C)=mr1;
  REG(CT2+0x14)=0;
  REG(CT2+0x34)=0x00200002u;
  REG(CT2+0x00)=2;
  REG(CT2+0x3C)=0x1Fu;
  REG(CT2+0x00)=1;
}
static void backlight_on(void){
  REG(0x40007034u)=0;
  pwm2(0x17700u,1);
  REG(0x40009000u)=0x5AFA0000u; REG(0x40008000u)=0x5AFA0000u;     /* like FU */
  REG(0x40018034u)=0x15555155u; REG(0x40019034u)=0x55540140u;
  pwm2(0,0);
  REG(CLK+0x20)=0x42u;
}
static void push_fb(void){
  lcd_cmd(0x2a,pcas,4); lcd_cmd(0x2b,pras,4);
  LCD_DC(0); { u8 c=0x2c; spi_tx(&c,1); }
  LCD_DC(1);
  u8* row=(u8*)0x1801A000u;                    /* one line, high byte first */
  for(u32 y=0;y<SCR_H;y++){
    for(u32 x=0;x<SCR_W;x++){ u16 p=fb[y*SCR_W+x]; row[2*x]=(u8)(p>>8); row[2*x+1]=(u8)p; }
#if PIX_BURST
    spi_tx(row, SCR_W*2);
#else
    /* 1.3.8: byte by byte like the FU (each byte on its own, wait for the end).
       In block mode bytes got lost (FIFO), every line shifted = skewed image. */
    for(u32 i=0;i<SCR_W*2;i++) spi_tx(&row[i],1);
#endif
  }
  LCD_DC(0);
}
static void draw(const u16* status, u32 num, u32 den){
#if ANZEIGE
  for(u32 i=0;i<SCR_W*SCR_H;i++) fb[i]=static_bg[i];
  for(int r=0;r<16;r++) for(int c=0;c<SCR_W;c++) fb[(STATUS_Y+r)*SCR_W+c]=status[r*SCR_W+c];
  u32 fillx = BAR_X0 + ((BAR_X1-BAR_X0)*num)/(den?den:1);
  for(u32 y=BAR_Y0;y<BAR_Y1;y++) for(u32 x=BAR_X0;x<fillx;x++) fb[y*SCR_W+x]=ACC565;
  push_fb();
#else
  (void)status; (void)num; (void)den;
#endif
}
#if ANZEIGE && HALTBILD
/* Push only lines y0..y1-1 to the panel. Fast enough to keep updating the bar
   during operation without sending the whole screen.
   Panel line = framebuffer line + 1 (like pras in push_fb). */
static void push_rows(u32 y0, u32 y1){
  u8 ca[4]={0x00,0x02,0x00,0x81};
  u32 rs=y0+1u, re=y1;
  u8 ra[4]={(u8)(rs>>8),(u8)rs,(u8)(re>>8),(u8)re};
  lcd_cmd(0x2a,ca,4); lcd_cmd(0x2b,ra,4);
  LCD_DC(0); { u8 c=0x2c; spi_tx(&c,1); }
  LCD_DC(1);
  u8* row=(u8*)0x1801A000u;
  for(u32 y=y0;y<y1;y++){
    for(u32 x=0;x<SCR_W;x++){ u16 p=fb[y*SCR_W+x]; row[2*x]=(u8)(p>>8); row[2*x+1]=(u8)p; }
    for(u32 i=0;i<SCR_W*2u;i++) spi_tx(&row[i],1);
  }
  LCD_DC(0);
}
/* Redraw the status line and bar and push only that strip. */
static void draw_live(const u16* status, u32 num, u32 den){
  for(u32 y=STATUS_Y;y<BAR_Y1;y++) for(u32 x=0;x<SCR_W;x++) fb[y*SCR_W+x]=static_bg[y*SCR_W+x];
  for(int r=0;r<16;r++) for(int c=0;c<SCR_W;c++) fb[(STATUS_Y+r)*SCR_W+c]=status[r*SCR_W+c];
  u32 fillx = BAR_X0 + ((BAR_X1-BAR_X0)*num)/(den?den:1);
  for(u32 y=BAR_Y0;y<BAR_Y1;y++) for(u32 x=BAR_X0;x<fillx;x++) fb[y*SCR_W+x]=ACC565;
  push_rows(STATUS_Y, BAR_Y1);
}
#endif

/* ============================ PAU1 ============================ */
static u32 rd32(const u8* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((u32)p[3]<<24); }
static void wr32(u8* p, u32 v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void wr16(u8* p, u16 v){ p[0]=v; p[1]=v>>8; }

/* sends a finished frame (without CRC/COBS) of length body in outf */
static void send_frame(u32 bodylen){
  wr32(outf+bodylen-4, crc32_buf(0xFFFFFFFFu, outf, bodylen-4) ^ 0xFFFFFFFFu);
  u32 cl=cobs_encode(outf, bodylen, cob);
  putb(0);                      /* frame delimiter */
  for(u32 i=0;i<cl;i++) putb(cob[i]);
  putb(0);
}

/* builds a reply header: type/seq/offset/status/payload length; returns payload ptr */
static u8* reply_head(u8 type, u32 seq, u32 off, u16 status, u16 plen){
  outf[0]=0x50; outf[1]=0x41; outf[2]=0x55; outf[3]=0x31;
  outf[4]=1; outf[5]=1; outf[6]=type; outf[7]=0;
  wr32(outf+8, 0); wr32(outf+12, seq); wr32(outf+16, off);
  wr16(outf+20, plen); wr16(outf+22, status);
  return outf+24;
}

static void handle(const u8* f, u32 flen){
  if(flen<28) return;
  if(!(f[0]==0x50&&f[1]==0x41&&f[2]==0x55&&f[3]==0x31)) return;
  u16 plen=f[20]|(f[21]<<8);
  if(flen < 28u+plen) return;
  if( (crc32_buf(0xFFFFFFFFu,f,28+plen-4)^0xFFFFFFFFu) != rd32(f+28+plen-4) ) return;
  u8 type=f[6];
  u32 seq=rd32(f+12), off=rd32(f+16);
  const u8* pl=f+24;

  if(type==2){ /* ECHO */
    u8* p=reply_head(2,seq,off,0,plen);
    for(u32 i=0;i<plen;i++) p[i]=pl[i];
    send_frame(28+plen); return;
  }
  if(type==1){ /* HELLO -> INFO 44 bytes */
    u8* p=reply_head(1,seq,off,0,44);
    for(int i=0;i<44;i++) p[i]=0;
    wr32(p+0, 0x00010000u);  /* appRevision */
    wr32(p+4, 0x00010000u);  /* loaderRevision */
    p[8]=1;                  /* edition (land, display) */
    p[9]=1;                  /* wave */
    wr16(p+10, (u16)MAXP);   /* maxBlock */
    wr32(p+16, 460800u);     /* baud */
    wr32(p+24, 0x00C22018u); /* jedecId MX25L12833F */
    wr32(p+32, FLASH_SIZE);  /* capacity */
    wr32(p+36, 0);           /* flashError */
    send_frame(28+44); return;
  }
  if(type==5){ /* READ len@payload at off */
#if WRITETEST
    if(off==0){
      u8* p=reply_head(5,seq,0,0,(u16)(plen>=4?rd32(pl):1024)); u32 L=plen>=4?rd32(pl):1024; if(L>MAXP)L=MAXP;
      for(u32 i=0;i<L;i++) p[i]=0;
      u8* src=(u8*)0x1801B000u; for(u32 i=0;i<0x1000;i++) src[i]=(u8)(i^0x5A);
      flash_read(0xA00000u, DMABUF, 16); for(int i=0;i<16;i++) p[i]=dma[i];
      prog_sector_fu(0xA00000u, 0x1801B000u);
      flash_read(0xA00000u, DMABUF, 32); for(int i=0;i<32;i++) p[16+i]=dma[i];
      flash_read(0x0u, DMABUF, 8);  for(int i=0;i<8;i++) p[48+i]=dma[i];
      flash_read(0xEFE000u, DMABUF, 8); for(int i=0;i<8;i++) p[56+i]=dma[i];
      flash_read(0x900000u, DMABUF, 8); for(int i=0;i<8;i++) p[64+i]=dma[i];
      flash_read(0xB00000u, DMABUF, 8); for(int i=0;i<8;i++) p[72+i]=dma[i];
      send_frame(28+L); return;
    }
#endif
#if ANZEIGE
    arm_for_dump();
#if HALTBILD
    if((off & 0x000FFFFFu)==0) draw_live(st_lesen, off, FLASH_SIZE);
#endif
#endif
    u32 len=plen>=4?rd32(pl):MAXP; if(len>MAXP) len=MAXP;
#if WRITEPROBE
    /* READ-ONLY PROBE: shows the real controller registers so the write path
     * can be built from real values. Does not write or erase anything. */
    if(off==0){
      u8* p=reply_head(5,seq,off,0,(u16)len);
      for(u32 i=0;i<len;i++) p[i]=0;
      flash_read(0xA00000u, DMABUF, 16); for(int i=0;i<16;i++) p[i]=dma[i];   /* free block, expect FF */
      wr32(p+16, REG(FCTL+0x000));   /* STAT */
      wr32(p+20, REG(FCTL+0x004));   /* CMD  */
      wr32(p+24, REG(FCTL+0x014));   /* DATA */
      wr32(p+28, REG(FCTL+0x018));   /* JEDEC register */
      wr32(p+32, REG(FCTL+0x01C));
      /* WREN attempt: opcode 0x06 in 0x010, set write bit (11) in 0x004 */
      REG(FCTL+0x010)=0x06u; REG(FCTL+0x004)|=(1u<<11);
      for(volatile int i=0;i<5000;i++){}
      wr32(p+36, REG(FCTL+0x004));   /* CMD afterwards (bit 11 cleared?) */
      wr32(p+40, REG(FCTL+0x014));
      /* status read attempt: opcode 0x05 in 0x010, read bit (14) in 0x004 */
      REG(FCTL+0x010)=0x05u; REG(FCTL+0x004)|=(1u<<14);
      for(volatile int i=0;i<5000;i++){}
      wr32(p+44, REG(FCTL+0x004));
      wr32(p+48, REG(FCTL+0x014));   /* <- chip status register, if readable this way */
      send_frame(28+len); return;
    }
#endif
    flash_read(off, DMABUF, len);
    u8* p=reply_head(5,seq,off,0,(u16)len);
    for(u32 i=0;i<len;i++) p[i]=dma[i];
    send_frame(28+len);
#if ANZEIGE && HALTBILD
    /* Last block of the dump: set the display to "done", otherwise "reading..."
       would stay on screen (a dump has no finish command). */
    if(off + len >= FLASH_SIZE) draw_live(st_fertig, 1, 1);
#endif
    return;
  }
  if(type==6){ /* CRC len@payload at off -> 4 bytes */
#if ANZEIGE
    arm_for_dump();
#if HALTBILD
    if((off & 0x000FFFFFu)==0) draw_live(st_schreiben, off, FLASH_SIZE);
#endif
#endif
    u32 len=plen>=4?rd32(pl):0;
    u32 c=crc32_flash(off,len);
    u8* p=reply_head(6,seq,off,0,4); wr32(p,c);
    send_frame(28+4); return;
  }
  if(type==13){ /* WRITE_STATE -> 16 bytes, tested=1 (skip self test) */
    u8* p=reply_head(13,seq,off,0,16);
    for(int i=0;i<16;i++) p[i]=0; wr32(p+12,1);
    send_frame(28+16); return;
  }
  if(type==8){ /* WRITE_TEST */
    u8* p=reply_head(8,seq,off,0,4); wr32(p,0); send_frame(28+4); return;
  }
  if(type==9){ /* WRITE_BEGIN -> startSector 0 */
#if ANZEIGE && HALTBILD
    /* Switch to "writing" right away, otherwise the "done" from the dump stays
       on screen until the first sector on a 1 MB boundary. */
    *g_wrcnt=0; draw_live(st_schreiben, 0, 1);
#endif
    u8* p=reply_head(9,seq,off,0,4); wr32(p,0); send_frame(28+4); return;
  }
  if(type==10){ /* WRITE_DATA: payload to SECBUF at off&0xFFF */
    u32 so=off & 0xFFFu;
    for(u32 i=0;i<plen && so+i<0x1000u;i++) sec[so+i]=pl[i];
    u8* p=reply_head(10,seq,off,0,0); send_frame(28+0); return;
  }
  if(type==11){ /* WRITE_COMMIT: erase sector off, write 4 KB, 8 byte reply */
#if ANZEIGE
    arm_for_dump();
#endif
    u32 base=off & ~0xFFFu;
#ifdef FW_PRECHECK
    /* 1.6.13 test16: the CRC sent along must match the buffer before anything is erased (a lost or doubled data
       packet): otherwise nothing is written, status 2, the tool sends the sector again */
    if(plen>=4){
      u32 want=rd32(pl), have=crc32_buf(0xFFFFFFFFu, sec, 0x1000u)^0xFFFFFFFFu;
      if(have!=want){ u8* p=reply_head(11,seq,off,0,8); wr32(p,have); wr32(p+4,2); send_frame(28+8); return; }
    }
#endif
    prog_sector_fu(base, SECBUF);            /* erase + program (our own code) */
#if ANZEIGE && HALTBILD
    /* Bar based on counted sectors (keeps the display moving forward, since the
       write order is not sequential). Redraw every 16 sectors. */
    { u32 n=++(*g_wrcnt); if((n & 0x0Fu)==0) draw_live(st_schreiben, n, FLASH_SIZE/0x1000u); }
#endif
    u32 c=crc32_flash(base, 0x1000u);        /* CRC read back (XIP), checked by the tool */
    /* Byte compare on the device: 0 = sector matches, otherwise error status 1 */
    u32 bad=0; flash_read(base, DMABUF, 0x1000u);
    for(u32 i=0;i<0x1000u;i++){ if(dma[i]!=sec[i]){ bad=1; break; } }
    u8* p=reply_head(11,seq,off,0,8); wr32(p,c); wr32(p+4,bad);
    send_frame(28+8); return;
  }
  if(type==12){ /* WRITE_FINISH */
#if ANZEIGE && HALTBILD
    draw_live(st_fertig, 1, 1);
#endif
    u8* p=reply_head(12,seq,off,0,0); send_frame(28+0); return;
  }
  if(type==14){ /* CLOCK_READ (1.3.0): the Paradise's seconds counter and RTC control -> 8 bytes */
    u8* p=reply_head(14,seq,off,0,8); wr32(p, REG(0x45000304u)); wr32(p+4, REG(0x45000200u));
    send_frame(28+8); return;
  }
  if(type==15){ /* CLOCK_SET (1.3.0): seconds counter := payload (only with the RTC running) -> 4 bytes back */
    u32 ok=0;
    if(plen>=4 && (REG(0x45000200u) & 0x80u)){ REG(0x45000304u)=rd32(pl); ok=1; }
    u8* p=reply_head(15,seq,off,ok?0:1,4); wr32(p, REG(0x45000304u));
    send_frame(28+4); return;
  }
  if(type==3){ /* CANCEL */
    u8* p=reply_head(3,seq,off,0,0); send_frame(28+0); return;
  }
}

static void pau1_loop(void){
  u32 w=0;
  for(;;){
    int b=getb();
    if(b==0){ if(w>0){ u32 dl=cobs_decode(rxac,w,frm); handle(frm,dl); } w=0; }
    else { if(w<8000u) rxac[w++]=(u8)b; else w=0; }   /* RXACC 8 KB */
  }
}

/* ===== full startup sequence, following the firmware (SystemInit) ===== */
/* Quiet the watchdog AND set up the key-protected system/power block (0x40008000)
 * like the original flash utility. 0x40008008=0x5AFA00FF enables the
 * peripheral/display power domains (without it the display has no power and
 * the screen stays dark). We leave the watchdog enable bit (0x40008000 bit0)
 * at 0 so the long dump is not interrupted by a watchdog reset. */
static void wdt_off(void){
  /* Set up the system/power block EXACTLY like the original flash utility. The
     only place where we deliberately differed before (bit0 of 0x40008000) might
     be the master enable for the display domain. The FU runs with bit0=1 without
     a reset, so we copy it 1:1 and feed the watchdog in the loop. */
  REG(0x4000900Cu)=0x5AFA55AAu; REG(0x40009000u)=0x5AFA0000u;   /* WDT1 */
  REG(0x40008000u)=0x5AFA0000u;                                  /* unlock */
  REG(0x40008004u)=0x5AFA0002u;
  REG(0x40008008u)=0x5AFA00FFu;   /* power/clock domains on (incl. display) */
  REG(0x4000800Cu)=0x5AFA55AAu;   /* feed the watchdog */
  /* Do NOT set bit0 (watchdog enable): otherwise it reset the Tama during
     display init (back to the planet). */
}
/* Clear the mailbox RAM (like 0x8b9c). */
static void mailbox_clear(void){
  REG(0x20000000u)=0; REG(0x20000004u)=0; REG(0x20000008u)=0; REG(0x2000000Cu)=0;
}
/* Enable the peripheral clock (like 0x8bc0: 0x45000020 = 0x40). */
static void periph_enable(void){ REG(CLK+0x20)=0x40u; }
/* Pin mux (like 0x899c, tables from the firmware). Without this the UART
   contacts are not connected. Writes (mode<<4) to 0xE000E400 + pinId. */
static void pin_set(u8 id, u8 mode){ *(volatile u8*)(0xE000E400u + id) = (u8)(mode<<4); }
static void pin_mux(void){
  static const u8 m12[]={6,7,8,9,0xf,0x10,0x11,0x12,0x13,0x14,0x19,0x1f,0x20,0x22,
    0x24,0x25,0x2e,0x2f,0x35,0x36,0x1a,0x1b,0x1c,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x3a,0x3f};
  static const u8 m8[]={0,1,2,3};
  static const u8 m4[]={0x15,0x17};
  for(unsigned i=0;i<sizeof m12;i++) pin_set(m12[i],0xC);
  for(unsigned i=0;i<sizeof m8;i++) pin_set(m8[i],0x8);
  for(unsigned i=0;i<sizeof m4;i++) pin_set(m4[i],0x4);
}
/* 1.3.9: put the flash pins (P1.0 to P1.4, ID 0x10 to 0x14) back to their flash
 * function and set up the flash controller again. The display setup touches the
 * GPIO1 block and can change the flash lines, after which the flash only reads
 * 0xFF (empty dump). We call this after the display setup and after every draw
 * so reading works again. */
static void flashctl_init(void);
/* 1.3.11: reset the flash pins (P1.0 to P1.4) to their flash function and set up
 * the flash controller again. Once after the display setup, in case the display's
 * GPIO1 writes touched the flash lines. NO clock_init here (it broke the UART
 * baud rate -> the Tama stopped answering). */
#if ANZEIGE
/* Take the panel off the bus completely while dumping: display off, sleep, panel
 * SPI controller off (frees SCLK/MOSI), CS high, reset held, backlight off. The
 * flash shares the SPI bus with the panel, so reading and drawing cannot run at
 * the same time. The screen goes dark during the dump, that is intended. */
#if !HALTBILD
static void panel_down(void){
  lcd_cmd(0x28,0,0);                 /* DISPON off */
  lcd_cmd(0x10,0,0); MS(20);         /* SLPIN */
  SPI_CTRL &= ~0xC0u;                /* FIFOs */
  SPI_CTRL &= ~1u;                   /* panel SPI off, SCLK/MOSI free */
  BB(GPIO1+0x00,5)=1;                /* panel CS off (P1.5 high) */
  LCD_RST(0);                        /* hold panel in reset */
  REG(CT2+0x00)=0;                   /* backlight PWM off */
  MS(5);
}
#endif
static u8* const g_armed=(u8*)0x1801A800u;   /* flag in fixed SRAM, no .bss */
/* On the first dump command, restore ONCE the complete, known good environment
 * of the dumper without display: panel off the bus, then the full boot setup
 * incl. clock_init. Then set up the UART again, otherwise the baud rate is wrong
 * after clock_init and the Tama does not answer (that was the bug in 1.3.10).
 * This way the flash reads again like in the version without display. */
static void arm_for_dump(void){
  if(*g_armed) return;
  *g_armed=1;
#if HALTBILD
  /* Keep the panel on. Only confirm the flash controller/XIP once more, in case
     the display setup changed something. Deliberately NO clock_init, pin_mux or
     uart_init: that would disturb the baud rate and the backlight. */
  flashctl_init();
#else
  panel_down();
  clock_init();
  periph_enable();
  flashctl_init();
  pin_mux();
  uart_init();
#endif
}
#endif

void Reset_Handler(void){
  wdt_off();
  mailbox_clear();
  clock_init();
  periph_enable();
  flashctl_init();
  pin_mux();
  uart_init();
#if ANZEIGE
  display_power();
  display_init();
  draw(st_bereit, 0, 1);
  backlight_on();
  *g_armed=0;   /* dump switch not done yet; screen stays on while idle */
#endif
  pau1_loop();
  for(;;){}
}

__attribute__((section(".vectors"), used))
const void* const g_vectors[] = { (void*)0x1801F000u, (void*)Reset_Handler };
