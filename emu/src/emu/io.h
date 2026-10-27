#ifndef IO_H
#define IO_H
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#ifndef REG
#define REG(a) (*(volatile u32*)(a))
#endif
/* Paradise flash through the uncached XIP window (the host test maps it to an array) */
#ifndef XIP_PTR
#define XIP_PTR(a) ((const volatile u8*)(0x60000000u + (a)))
#endif
#define UART  0x4000B000u
#define FCTL  0x40022000u
#define CLK   0x45000000u
static inline void dsb(void){ __asm__ volatile("dsb":::"memory"); }

#ifndef TC_INIT
#ifdef EMU_OVL
#define TC_INIT __attribute__((section(".initcode"), noinline, minsize))
#else
#define TC_INIT __attribute__((section(".initcode"), noinline))
#endif
#endif
#endif
