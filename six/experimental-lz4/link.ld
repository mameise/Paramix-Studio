ENTRY(lz4_enter)
legacy_enter = 0x1007B9FA;
legacy_dispatch = 0x1007BA26;
legacy_return = 0x1007BA34;
SECTIONS {
  . = 0x100F7000;
  .text : { KEEP(*(.text.hooks)) *(.text*) *(.rodata*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
