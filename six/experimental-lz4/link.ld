ENTRY(lz4_enter)
SECTIONS {
  . = 0x100F7000;
  .text : { KEEP(*(.text.hooks)) *(.text*) *(.rodata*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
