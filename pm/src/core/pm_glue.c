/* PokeMiniEmu: what PokeMini.c, PMCommon.c and the colour PRC give the core, cut down for the Paradise (no files,
 * no colour, no multicart). The cartridge is read straight from the cached flash window (PM_ROM points there).
 * Based on PokeMini by JustBurn (GPL 3 or later), see LICENSE_pokemini. Author of the changes: mameise */
#include "PokeMini.h"
#include "freebios.h"
#include <string.h>

int PokeMini_FreeBIOS = 0, PokeMini_Flags = 0, PokeMini_LCDMode = LCDMODE_ANALOG, PokeMini_ColorFormat = 0, PokeMini_HostBattStatus = 0;
uint8_t PM_BIOS[4096];
uint8_t PM_RAM[8192];
uint8_t *PM_ROM;
int PM_ROM_Alloc, PM_ROM_Size, PM_ROM_Mask;
int PokeHWCycles;

static const uint8_t PM_IO_INIT[256] = {
	0x7F, 0x20, 0x5C, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
	0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
	0x00, 0x30, 0x02, 0x00, 0x02, 0x00, 0x00, 0x40, 0x00, 0xC0, 0x40, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
	0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
	0xFF, 0x00, 0xFF, 0x00, 0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x32, 0x64, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x40, 0xFF };

/* ---- memory: a small heap for the core's buffers (EEPROM, LCD) ---- */
#ifndef PM_HEAP
#define PM_HEAP 0x5C00
#endif
static uint8_t g_heap[PM_HEAP] __attribute__((aligned(8)));
static uint32_t g_heap_n;
#ifndef PM_HOST
void *malloc(size_t n) { n = (n + 7u) & ~7u; if (g_heap_n + n > sizeof g_heap) return 0; void *p = g_heap + g_heap_n; g_heap_n += n; return p; }
void free(void *p) { (void)p; }
#endif

int GetMultiple2Mask(int input)
{
	if (input) input--;
	input |= input >> 1; input |= input >> 2; input |= input >> 4; input |= input >> 8; input |= input >> 16;
	return input;
}

/* ---- the colour PRC is not there ---- */
int PRCColorEnable; uint8_t *PRCColorPixels, *PRCColorPixelsOld, *PRCColorMap, *PRCColorTop; unsigned int PRCColorOffset; uint8_t PRCColorFlags;
int MinxColorPRC_Create(void) { return 1; }
void MinxColorPRC_Reset(int hardreset) { (void)hardreset; }
uint8_t MinxColorPRC_ReadReg(int cpu, uint8_t reg) { (void)cpu; (void)reg; return 0xFF; }
void MinxColorPRC_WriteReg(uint8_t reg, uint8_t val) { (void)reg; (void)val; }
void MinxColorPRC_WriteFramebuffer(uint16_t addr, uint8_t data) { (void)addr; (void)data; }
void MinxColorPRC_WriteLCD(uint16_t addr, uint8_t data) { (void)addr; (void)data; }
void MinxPRC_NoRender_Color(void) {}

/* ---- the Pokemon mini ---- */
int PokeMini_Create(int flags, int soundfifo)
{
	(void)soundfifo;
	g_heap_n = 0;
	memset(PM_BIOS, 0xFF, 4096); memset(PM_RAM, 0xFF, 8192);
	PM_ROM_Mask = 0x1FFF; PM_ROM_Size = 0x2000; PM_ROM = PM_RAM;
	memcpy(PM_IO, PM_IO_INIT, 256);
	if (!MinxCPU_Create() || !MinxTimers_Create() || !MinxIO_Create() || !MinxIRQ_Create() || !MinxPRC_Create() || !MinxLCD_Create() || !MinxAudio_Create(0, 0)) return 0;
	PokeMini_Flags = flags;
	return 1;
}
int PokeMini_LoadFreeBIOS(void) { PokeMini_FreeBIOS = 1; memcpy(PM_BIOS, FreeBIOS, 4096); return 1; }
/* the cartridge in the flash window: size rounded up to a power of two (reads behind it repeat the start) */
void pm_set_rom(const uint8_t *rom, uint32_t len) { PM_ROM = (uint8_t *)rom; PM_ROM_Mask = GetMultiple2Mask((int)len); PM_ROM_Size = PM_ROM_Mask + 1; }
void PokeMini_Reset(int hardreset)
{
	if (hardreset) { memset(PM_RAM, 0xFF, 8192); memcpy(PM_IO, PM_IO_INIT, 256); }
	MinxTimers_Reset(hardreset); MinxIRQ_Reset(hardreset); MinxIO_Reset(hardreset); MinxPRC_Reset(hardreset);
	MinxLCD_Reset(hardreset); MinxAudio_Reset(hardreset); MinxCPU_Reset(hardreset);
}
/* the clock of the game from the Paradise's (the BIOS then skips asking for the time) */
void pm_set_time(int y, int mo, int d, int h, int mi, int s)
{
	MinxIO_SetTimeStamp((uint8_t)(y % 100), (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s);
	PMR_SEC_CTRL = 0x01; MinxTimers.SecTimerCnt = 0;
}

/* ---- the state in a buffer (as PokeMini's state files, without the colour part) ---- */
uint64_t memstream_read(memstream_t *s, void *d, uint64_t n) { if (s->pos + n > s->size) n = s->size - s->pos; memcpy(d, s->buf + s->pos, (size_t)n); s->pos += (uint32_t)n; return n; }
uint64_t memstream_write(memstream_t *s, const void *d, uint64_t n) { if (s->pos + n > s->size) n = s->size - s->pos; memcpy(s->buf + s->pos, d, (size_t)n); s->pos += (uint32_t)n; return n; }
int64_t memstream_seek(memstream_t *s, int64_t off, int whence) { int64_t p = whence == 1 ? s->pos + off : off; if (p < 0 || p > s->size) return -1; s->pos = (uint32_t)p; return 0; }
uint64_t memstream_pos(memstream_t *s) { return s->pos; }
#define PM_STATE_ID 0x31534D50u   /* "PMS1" */
static void tag(memstream_t *s, const char *t) { memstream_write(s, t, 4); }
uint32_t pm_state_save(uint8_t *buf, uint32_t size)
{
	memstream_t s = { buf, size, 0 }; uint32_t v = PM_STATE_ID, n;
	memstream_write(&s, &v, 4);
	tag(&s, "RAM-"); n = 0x1000; memstream_write(&s, &n, 4); memstream_write(&s, PM_RAM, 0x1000);
	tag(&s, "REG-"); n = 256; memstream_write(&s, &n, 4); memstream_write(&s, PM_IO, 256);
	tag(&s, "CPU-"); MinxCPU_SaveStateStream(&s);
	tag(&s, "IRQ-"); MinxIRQ_SaveStateStream(&s);
	tag(&s, "TMR-"); MinxTimers_SaveStateStream(&s);
	tag(&s, "PIO-"); MinxIO_SaveStateStream(&s);
	tag(&s, "PRC-"); MinxPRC_SaveStateStream(&s);
	tag(&s, "LCD-"); MinxLCD_SaveStateStream(&s);
	tag(&s, "AUD-"); MinxAudio_SaveStateStream(&s);
	tag(&s, "END-"); n = 0; memstream_write(&s, &n, 4);
	return s.pos < size ? s.pos : 0;   /* 0: did not fit */
}
int pm_state_load(uint8_t *buf, uint32_t size)
{
	memstream_t s = { buf, size, 0 }; uint32_t v = 0, n; char t[4];
	if (memstream_read(&s, &v, 4) != 4 || v != PM_STATE_ID) return 0;
	for (;;) {
		if (memstream_read(&s, t, 4) != 4 || memstream_read(&s, &n, 4) != 4) return 0;
		int ok = 1;
		if (!memcmp(t, "RAM-", 4)) ok = n == 0x1000 && memstream_read(&s, PM_RAM, 0x1000) == 0x1000;
		else if (!memcmp(t, "REG-", 4)) ok = n == 256 && memstream_read(&s, PM_IO, 256) == 256;
		else if (!memcmp(t, "CPU-", 4)) ok = MinxCPU_LoadStateStream(&s, n);
		else if (!memcmp(t, "IRQ-", 4)) ok = MinxIRQ_LoadStateStream(&s, n);
		else if (!memcmp(t, "TMR-", 4)) ok = MinxTimers_LoadStateStream(&s, n);
		else if (!memcmp(t, "PIO-", 4)) ok = MinxIO_LoadStateStream(&s, n);
		else if (!memcmp(t, "PRC-", 4)) ok = MinxPRC_LoadStateStream(&s, n);
		else if (!memcmp(t, "LCD-", 4)) ok = MinxLCD_LoadStateStream(&s, n);
		else if (!memcmp(t, "AUD-", 4)) ok = MinxAudio_LoadStateStream(&s, n);
		else if (!memcmp(t, "END-", 4)) return 1;
		else ok = memstream_seek(&s, n, 1) == 0;
		if (!ok) return 0;
	}
}
