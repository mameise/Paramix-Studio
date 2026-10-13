/* DOOM on the Paradise: PRAM (loaded by the stub), RAM at 0x20001000, program window 0x10000000, SRAM */
ENTRY(Reset_Handler)
MEMORY {
  PRAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  RAM20 (rwx) : ORIGIN = 0x20001000, LENGTH = 0xF000
  XIP   (rx)  : ORIGIN = 0x100A0000, LENGTH = 0x5D000
  SRAM  (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1E800
}
SECTIONS {
  .pram : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    KEEP(*(.pdmcfg))
    *(.pram_text*) *(.pram_rodata*) *(.initcode*) *(.initdata*)
    *hw.o(.text* .rodata*) *flash.o(.text* .rodata*)
    INCLUDE pram_hot.ld
    . = ALIGN(4);
  } > PRAM
  .pbss (NOLOAD) : { . = ALIGN(4); *(.pram_bss*) . = ALIGN(4); __pbss_end = .; } > PRAM
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  __pram_end = LOADADDR(.data) + SIZEOF(.data);
  .r20 : {
    __r20_start = .;
    INCLUDE r20_hot.ld
    . = ALIGN(4);
    __r20_end = .;
  } > RAM20 AT > XIP
  __r20_load = LOADADDR(.r20);
  .xip : { *(.text*) *(.rodata*) . = ALIGN(4); } > XIP
  .ARM.exidx : { *(.ARM.exidx*) } > XIP
  .r20bss (NOLOAD) : { __r20b_start = .; *(.ram20_bss*) . = ALIGN(4); __r20b_end = .; } > RAM20
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) *(.openings_bss*) . = ALIGN(8); *(.displayData_bss*) . = ALIGN(4); __bss_end = .; } > SRAM
  /DISCARD/ : { *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.eh_frame) *(.comment) *(.ARM.attributes) }
}
