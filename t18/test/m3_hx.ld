/* Memory as on the Paradise (src/app/link.ld): hot code and the code buffer in the PRAM, the rest of
 * the code in the flash window, stack and data in the SRAM hole, the rest of the SRAM is the guest's. */
ENTRY(Reset_Handler)
MEMORY { PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
         XIP  (rx)  : ORIGIN = 0x100DF000, LENGTH = 0x10000
         HOLE (rw)  : ORIGIN = 0x1800E600, LENGTH = 0x5E00 }
SECTIONS {
  .xip : {
    *(.xiptext*)
    *t18jit.o(.text.translate_thumb .text.translate_arm .text.t_decode .text.a_decode .text.emit_* .text.block_begin .text.block_end* .text.modimm_up .text.t2_* .text.get_reg .text.set_reg .text.flush .text.dp_operand* .text.a_shift* .text.cond_reads)
    *t18soc.o(.text.tg18_init .text.soc_reset .text.tg18_save_state .text.tg18_load_state .text.tg18_state_size .text.tg18_set_time .text.tg18_get_time .text.tg18_advance_offline .text.reg_clear .text.tg18_set_addrs .text.tg18_get_addrs .text.tg18_set_present .text.days_from_civil .text.find_idle .text.tg18_struct_size .text.tg18_sf_regp .text.tg18_sf_ctlp .text.tg18_sf_outp)
    *t18cpu.o(.text.arm7_init .text.arm7_reset .text.arm7_save .text.arm7_load .text.build_cond .text.t18_sram_clear)
    *t18soc.o(.text.io_wr .text.io_rd .text.tg18_run_cycles .text.tmr_io_rd .text.tmr_io_wr) *t18cpu.o(.text.arm7_step .text.exec_arm) *libgcc.a:_udivmoddi4.o(.text*)
    *m3main.o(.text.main)
    . = ALIGN(4);
  } > XIP
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) . = ALIGN(4); } > PRAM
  .stack (NOLOAD) : { . += 0x800; __stack_top = .; } > HOLE
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > HOLE AT > PRAM
  __data_load = LOADADDR(.data);
  __jc_start = 0x80C8;
  __jt_start = 0x10000 - 4096;
  __jc_end = __jt_start;
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > HOLE
  /DISCARD/ : { *(.comment) *(.ARM.*) *(.note*) }
}
