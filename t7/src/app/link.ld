ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  SRAM (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1F000
  XL   (r)   : ORIGIN = 0x00010000, LENGTH = 0x8000    /* 1.6.12 (T7_JIT): load place of the second part (t7x.bin) */
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(EXCLUDE_FILE(*t7_jit.o) .text*)
    *(EXCLUDE_FILE(*t7_jit.o) .rodata*)
    . = ALIGN(4);
    __pdata_start = .;
  } > PRAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  /* mutable data in SRAM (initial values in PRAM, copied at startup), as in TamaEmu */
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  .init : { . = ALIGN(4); KEEP(*(.initcode)) KEEP(*(.initdata*)) KEEP(*(.initcode_z)) . = ALIGN(4); __prog_end = .; } > PRAM
  /* the second part: the translator (t7_jit.c), in the SRAM behind .bss; the program copies it from the flash
   * (save area + 0x8000, written by the Studio) and runs without it when it is not there */
  .xcode ALIGN(8) : { __x_start = .; *t7_jit.o(.text* .rodata*) . = ALIGN(8); __x_end = .; } > SRAM AT > XL
  __x_load = LOADADDR(.xcode);
  ASSERT(__prog_end <= 0x10000, "program too big for PRAM")
  ASSERT(__x_end <= 0x1801F000, "RAM full")
  ASSERT(__bss_end <= 0x1801F000, "RAM full")
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
