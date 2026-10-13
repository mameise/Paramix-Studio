ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  SRAM (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1F000
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    __pdata_start = .;
  } > PRAM
  .bss (NOLOAD) : { __bss_start = .; KEEP(*(.stats)) *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  /* v0.8.16: mutable data lives in SRAM (initial values in PRAM, copied at startup). Writing to
   * PRAM while code runs from PRAM corrupts the fetched instructions on the device. */
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  __ram_end = __data_end;
  .init : {
    . = ALIGN(4);
    __initcode_start = .;
    KEEP(*(.initcode))
    KEEP(*(.initdata*))
    __initlast = .;
    KEEP(*(.initcode_z))
    . = ALIGN(4);
  } > PRAM
  /* jit_init writes the fixed routines (just over 2 KB) from __initcode_start while
   * it is running itself: it must be placed far enough behind them */
  ASSERT(__initlast - __initcode_start >= 0x1000, "jit_init is too close to the start of the slot")
  .jit (NOLOAD) : { . = ALIGN(4); __jit_start = .; . = 0x10000; } > PRAM
  __jit_end = 0x10000;
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
