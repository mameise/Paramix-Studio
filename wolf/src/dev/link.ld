/* Wolf3D on the Paradise: PRAM (64 KB, loaded by the menu: code and the start values of .data) and a second part of
 * code for the SRAM behind .bss, which lies behind the first 64 KB in wolf.bin; the program copies it from the flash
 * once the flash window is on again (the stub starts us with a system reset, which switches it off).
 * Not the RAM at 0x20000000: it has only 4 KB (mirrored), with the mailbox of the programs at its end. */
ENTRY(Reset_Handler)
MEMORY {
  PRAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  R20L  (r)   : ORIGIN = 0x00010000, LENGTH = 0x20000
  SRAM  (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1F000
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(EXCLUDE_FILE(*pmx_main.o *pmx_save.o *wl_game.o *wl_utils.o *lib_a-*printf*.o *lib_a-*vfprintf*.o *lib_a-*vfiprintf*.o *libc_a-*printf*.o *libc_a-*vfprintf*.o *libc_a-*vfiprintf*.o *_udivmoddi4.o) .text*)
    *(EXCLUDE_FILE(*pmx_main.o *pmx_save.o *wl_game.o *wl_utils.o) .rodata*)
    KEEP(*(.initcode)) KEEP(*(.initdata*)) KEEP(*(.initcode_z))
    . = ALIGN(4);
    __prog_end = .;
  } > PRAM
  ASSERT(__prog_end <= 0x10000, "program too big for PRAM")
  __pram_used = LOADADDR(.data) + SIZEOF(.data);
  ASSERT(__pram_used <= 0x10000, "program and its data too big for PRAM")
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  __load_end = LOADADDR(.data) + SIZEOF(.data);
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  .scode : {
    __sc_start = .;
    *(.text*) *(.rodata*)
    . = ALIGN(4);
    __sc_end = .;
  } > SRAM AT > R20L
  __sc_load = LOADADDR(.scode);
  ASSERT(__sc_end <= 0x1801C000, "RAM full (16 KB stay for the stack)")
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.eh_frame) }
}
