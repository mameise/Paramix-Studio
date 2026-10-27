/* TamaEmu with EMU_OVL (1.6.13 test10): like link.ld, but the settings menus (ask.c) and the code that runs only when
 * leaving or in the settings (.ovltext) sit at the end of the image, in the translator's room (the overlay). The app
 * copies them back from its image in the flash when they are needed (ovl_load). */
ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  SRAM (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1F000
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *ask.o(.text.keep*)
    *(EXCLUDE_FILE(*ask.o) .text*)
    *ask.o(.rodata.keep*)
    *(EXCLUDE_FILE(*ask.o) .rodata*)
    . = ALIGN(4);
    __pdata_start = .;
  } > PRAM
  .bss (NOLOAD) : { __bss_start = .; KEEP(*(.stats)) *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
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
    __ovl_start = .;
    *(.ovltext*)
    *(.ovlrodata*)
    *ask.o(.text* .rodata*)
    . = ALIGN(4);
    __ovl_end = .;
  } > PRAM
  ASSERT(__initlast - __initcode_start >= 0x1000, "jit_init is too close to the start of the slot")
  ASSERT(__ovl_start - __initcode_start >= 0x1400, "the overlay must lie behind the translator's fixed routines")
  .jit (NOLOAD) : { . = ALIGN(4); __jit_start = .; . = 0x10000; } > PRAM
  __jit_end = 0x10000;
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
