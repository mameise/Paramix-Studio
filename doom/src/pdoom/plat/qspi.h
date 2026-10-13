#ifndef QSPI_H
#define QSPI_H
#include <stdint.h>
uint32_t qspiFlashGetSize(void);
void qspiFlashErasePage4k(uint32_t address);
void qspiFlashProgram(uint32_t address, const void *buffer, uint32_t size);
#endif
