ENTRY(neon_growth_header)
SECTIONS {
    . = 0x100F6000;
    .text : {
        KEEP(*(.text.header))
        KEEP(*(.text.hook))
        *(.text .text.*)
        . = ALIGN(4);
        neon_growth_config = .;
        FILL(0x00000000)
        . += 330;
    }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
