#ifndef PARADISE_NRF_SHIM
#define PARADISE_NRF_SHIM
#include <stdint.h>
#define __DMB() __asm__ volatile("dmb" ::: "memory")
#define __DSB() __asm__ volatile("dsb" ::: "memory")
#define __ISB() __asm__ volatile("isb" ::: "memory")
#endif
/* Paradise: stand-in for the nRF52840 QSPI DMA. The flash is directly readable, reads happen immediately. */
#include <string.h>
typedef struct { struct { volatile uint32_t CNT, SRC, DST; } READ; volatile uint32_t EVENTS_READY; } plat_qspi_t;
extern plat_qspi_t plat_qspi;
#define NRF_QSPI (&plat_qspi)
static inline void plat_qspi_read(void){ memcpy((void*)plat_qspi.READ.DST, (const void*)plat_qspi.READ.SRC, plat_qspi.READ.CNT); plat_qspi.EVENTS_READY = 1; }
