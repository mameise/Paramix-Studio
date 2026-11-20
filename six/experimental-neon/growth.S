#ifndef GROWTH_SPECIES_SP
#define GROWTH_SPECIES_SP 0xc8
#define GROWTH_CARE_SP 0x38
#endif

.syntax unified
.thumb
.cpu cortex-m3

.section .text.header,"ax"
.global neon_growth_header
neon_growth_header:
  .word 0x5747524E             @ NRGW
  .word 2                      @ automatic field-based growth
  .word neon_growth_hook - neon_growth_header
  .word neon_growth_config - neon_growth_header

.section .text.hook,"ax"
.thumb_func
.global neon_growth_hook
neon_growth_hook:
  push {r0,r2,r3,lr}           @ r0 is the native settings payload
  ldrh.w r1,[sp,#(GROWTH_SPECIES_SP + 16)]         @ species selected by the original growth routine
  ldr r2,[sp,#(GROWTH_CARE_SP + 16)]            @ original adult lineage and care index
  bl neon_growth_pick
  strh.w r0,[sp,#(GROWTH_SPECIES_SP + 16)]         @ later native growth calls reuse this species
  mov r1,r0
  pop {r0,r2,r3,pc}           @ return the selected species in r1
