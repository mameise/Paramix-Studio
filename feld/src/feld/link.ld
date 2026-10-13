ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  SRAM (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1E000
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(.text*)
    *(.rodata*)
    KEEP(*(.initcode))
    KEEP(*(.initdata*))
    . = ALIGN(4);
  } > PRAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  ASSERT(SIZEOF(.data) == 0, "no initialised data allowed")
  .data : { *(.data*) } > SRAM
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
