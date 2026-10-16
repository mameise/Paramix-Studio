ENTRY(Reset_Handler)
MEMORY { PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x40000
         DATA (rw)  : ORIGIN = 0x20000000, LENGTH = 0x10000 }
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) . = ALIGN(4); } > PRAM
  .data : { *(.data*) . = ALIGN(4); } > PRAM
  .jitglue : { . = ALIGN(4); __glue_start = .; *(.jitglue) . = ALIGN(4); __glue_end = .; } > PRAM
  __glue_load = LOADADDR(.jitglue);
  .jitbuf (NOLOAD) : { . = ALIGN(4); *(.jitbuf) } > PRAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > DATA
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
