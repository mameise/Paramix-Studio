/* T18 app on the Paradise. The RAM at 0x20000000 has only 4 KB (the Paradise firmware's), so:
 *   PRAM 0x00000000 (64 KB, loaded by the stub): hot code; from __jc_start on (over the .data image and
 *     the start code) the code buffer of the translator.
 *   flash window (0x10000000 + flash offset - 0x11000): the rest of the code, flash T18_XIP_OFF (0xF0000).
 *   SRAM hole 0x1800E600..0x18014400: stack (bottom), data. The rest of the SRAM is the guest's. */
ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  XIP  (rx)  : ORIGIN = 0x100DF000, LENGTH = 0x10000
  HOLE (rw)  : ORIGIN = 0x1800E600, LENGTH = 0x5E00
}
SECTIONS {
  .xip : {
    __xip_magic = .;
    KEEP(*(.xipmagic))
    *(.xiptext*)
    *t18jit.o(.text.translate_thumb .text.translate_arm .text.t_decode .text.a_decode .text.emit_* .text.block_begin .text.block_end* .text.modimm_up .text.t2_* .text.get_reg .text.set_reg .text.flush .text.dp_operand* .text.a_shift* .text.cond_reads)
    *t18soc.o(.text.tg18_init .text.soc_reset .text.tg18_save_state .text.tg18_load_state .text.tg18_state_size .text.tg18_set_time .text.tg18_get_time .text.tg18_advance_offline .text.reg_clear .text.tg18_set_addrs .text.tg18_get_addrs .text.tg18_set_present .text.days_from_civil .text.find_idle .text.tg18_struct_size .text.tg18_sf_regp .text.tg18_sf_ctlp .text.tg18_sf_outp)
    *t18cpu.o(.text.arm7_init .text.arm7_reset .text.arm7_save .text.arm7_load .text.build_cond .text.t18_sram_clear)
    /* 1.6.6: rarely run, out of the PRAM for a bigger code buffer (T18_XIP_MORE) */
    XIPMORE
    *(.rodata.t18_zero_pg)
    . = ALIGN(4);
    __xip_end = .;
  } > XIP
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > PRAM
  .stack (NOLOAD) : { __stack_bottom = .; . += 0x600; __stack_top = .; } > HOLE
  .data : { __data_start = .; *(.ramfunc*) *(.data*) . = ALIGN(4); __data_end = .; } > HOLE AT > PRAM
  __data_load = LOADADDR(.data);
  .init : { KEEP(*(.initcode)) KEEP(*(.initdata*)) KEEP(*(.initcode_z)) . = ALIGN(4); } > PRAM
  __prog_end = .;
  __jc_start = ALIGN(__data_load, 4);
  __jt_start = 0x10000 - JTSIZE;   /* the translator's table for indirect jumps (8 << JT_BITS bytes, build.sh) */
  __jc_end = __jt_start;
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > HOLE
  ASSERT(__prog_end <= 0xC000, "PRAM: too little room left for the translated code")
  ASSERT(__xip_end - 0x100DF000 <= 0x10000, "flash part too big")
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
