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
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  /* mutable data in SRAM (initial values in PRAM, copied at startup), as in TamaEmu */
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  .init : { . = ALIGN(4); KEEP(*(.initcode)) KEEP(*(.initdata*)) KEEP(*(.initcode_z)) . = ALIGN(4); __prog_end = .; } > PRAM
  ASSERT(__prog_end <= 0x10000, "program too big for PRAM")
  ASSERT(__bss_end <= 0x1801F000, "RAM full")
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
