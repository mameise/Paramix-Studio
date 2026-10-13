#ifndef SRC_GRAPHICS_H_
#define SRC_GRAPHICS_H_
#include <stdbool.h>
#include <stdint.h>
#ifndef PDOOM_W
#define PDOOM_W 128
#define PDOOM_H 128
#endif
typedef struct
{
    volatile uint16_t * pPalette;
#ifdef SINGLE_FB
    uint8_t *displayFrameBuffer[2];   /* v0.2.2: both point to the same framebuffer in SRAM */
#else
    uint8_t displayFrameBuffer[2][PDOOM_W * PDOOM_H];
#endif
    uint8_t workingBuffer;
    uint8_t displayMode;
    volatile uint8_t dmaBusy;
} displayData_t;
extern displayData_t displayData;
void displayPrintf(int x, int y, const char * format, ...);
void startDisplayRefresh(uint8_t bufferNumber);
void setDisplayPen(int color, int background);
void clearScreen4bpp(void);
void displayPrintln(bool update, const char * format, ...);
void initGraphics(void);
#endif
