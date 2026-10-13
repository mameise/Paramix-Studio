/* Temporary placeholders, only for measuring sizes */
#include <stdint.h>
#include "graphics.h"
#include "pwm_audio.h"
#include "nrf.h"
displayData_t displayData;
soundChannel_t soundChannels[MAX_CHANNELS];
plat_qspi_t plat_qspi;
void SetBacklight(int x){(void)x;}
void delay(unsigned int ms){(void)ms;}
void disableWirelessAudio(void){}
void restoreWirelessAudio(void){}
void getKeys(uint16_t *k){*k=0;}
void muteSound(void){}
void updateSound(void){}
void plat_flash_erase4k(uint32_t a){(void)a;}
void plat_flash_word(uint32_t a, uint32_t w){(void)a;(void)w;}
unsigned int plat_micros(void){return 0;}
uint32_t qspiFlashGetSize(void){return 0x1000000;}
void qspiFlashErasePage4k(uint32_t a){(void)a;}
void qspiFlashProgram(uint32_t a, const void *b, uint32_t s){(void)a;(void)b;(void)s;}
void startDisplayRefresh(uint8_t b){(void)b;}
void _putchar(char c){(void)c;}
int main(void){ extern void D_DoomMain(void); D_DoomMain(); return 0; }
