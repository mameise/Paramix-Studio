ENTRY(Reset_Handler)
SECTIONS {
  .text 0x0 : {
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  }
  /DISCARD/ : { *(.data*) *(.bss*) *(COMMON) *(.comment) *(.ARM.*) *(.note*) }
}
