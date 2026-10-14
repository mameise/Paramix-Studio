@ Paramix Studio: more fields for Tropical/Glacier (Land, Water, Sky added) and Jade Forest (Sky added).
@ One 4 KB page at flash 0x10F000 (XIP 0x100FE000). The Studio patches the firmware to call
@ the code here and fills in every address and table below. A set byte in the settings block
@ (offset 0xF04) says which tables the field slots use: 0 = the original ones, 1 = the
@ alternative ones (Land, Water, Sky on Tropical/Glacier, Sky in slot 1 on Jade Forest).
.syntax unified
.thumb
.text
.global _start
_start:
hdr:    .word 0x31584953          @ 0x00 "SIX1"
        .word 5                   @ 0x04 version (4: plain values in the site list, 5: planet)
        .word 0                   @ 0x08 flag address (RAM)
        .word 0xF04               @ 0x0C flag offset in the settings block
        .word 0, 0, 0             @ 0x10 sprites, composites, scripts before (for removing)
        .word 0                   @ 0x1C kind: 0 Tropical/Glacier, 1 Jade Forest
        .word 0                   @ 0x20 options: bit 0 planet of the field
        .space 0x40-0x24

@ r12 = flag (0 or 1), flags set. Keeps every other register.
.macro FLAG
  ldr r12,=_start
  ldr r12,[r12,#8]
  ldrsb r12,[r12]
  cmp r12,#0
.endm

@ 0x040: background composites of a field (close-up, field view): b.w from the firmware's
@ function. r0 = field (s8), r1 = variant (u8). Mode 0: table + field*64 + variant*32
@ (Tropical/Glacier), mode 1: table + field*32 (Jade Forest).
.thumb_func
six_bg:
  sxtb r0,r0
  uxtb r1,r1
  push {r4}
  ldr r2,=w_bg
  ldr r4,[r2,#8]
  FLAG
  ite ne
  ldrne r3,[r2,#4]
  ldreq r3,[r2]
  cmp r4,#0
  ite eq
  addeq r3,r3,r0,lsl #6
  addne r3,r3,r0,lsl #5
  it eq
  addeq r3,r3,r1,lsl #5
  mov r0,r3
  pop {r4}
  bx lr

@ child id in r0, the index lies in the caller's stack at the offset w_child+8
.thumb_func
six_child:
  push {r1}
  ldr r1,=w_child
  ldr r0,[r1,#8]
  add r0,sp,r0
  ldr r0,[r0,#4]            @ +4: the pushed r1
  FLAG
  ite ne
  ldrne r1,[r1,#4]
  ldreq r1,[r1]
  ldrh r0,[r1,r0,lsl #1]
  pop {r1}
  bx lr

@ Table base for any place that loaded a per field table with movw/movt into r0 or r1:
@ bl from that place. The return address picks the entry in the site list (return address,
@ original table, alternative table). The entries are plain words, so a place that loads two
@ halfword values (special characters) works the same way. Every other register is kept.
.macro TAB reg, keep
  push {\keep,r2,r3}
  ldr r2,=t_sites
1:ldr r3,[r2]
  cmp r3,#0
  beq 3f
  cmp r3,lr
  beq 2f
  adds r2,#12
  b 1b
2:ldr r3,=_start
  ldr r3,[r3,#8]
  ldrsb r3,[r3]
  cmp r3,#0
  ite ne
  ldrne \reg,[r2,#8]
  ldreq \reg,[r2,#4]
3:pop {\keep,r2,r3}
  bx lr
.endm
.thumb_func
six_tab_r0:
  TAB r0, r1
.thumb_func
six_tab_r1:
  TAB r1, r0

@ Same for a table loaded into r2 (planet stages).
.thumb_func
six_tab_r2:
  push {r0,r3}
  ldr r0,=t_sites
1:ldr r3,[r0]
  cmp r3,#0
  beq 3f
  cmp r3,lr
  beq 2f
  adds r0,#12
  b 1b
2:ldr r3,=_start
  ldr r3,[r3,#8]
  ldrsb r3,[r3]
  cmp r3,#0
  ite ne
  ldrne r2,[r0,#8]
  ldreq r2,[r0,#4]
3:pop {r0,r3}
  bx lr

@ Planet row: replaces the call that turns the device edition into a row of the planet
@ tables. With the set byte, the field slot picks the row from w_pmap+8 (0xFF = as before).
.thumb_func
six_pmap:
  FLAG
  beq 1f
  ldr r12,=w_pmap
  push {r1}
  ldr r1,[r12,#4]
  ldrsb r1,[r1]               @ field slot 0..2
  cmp r1,#2
  bhi 4f
  add r1,r12,r1
  ldrb r1,[r1,#8]
  cmp r1,#0xFF
  beq 4f
  mov r0,r1
  pop {r1}
  bx lr
4:pop {r1}
1:ldr r12,=w_pmap
  ldr r12,[r12]
  bx r12
.ltorg
.org 0x1b0
w_pmap:  .word 0, 0, 0xFFFFFFFF   @ original function, field byte, row per slot (bytes)
.org 0x1c0
w_bg:    .word 0, 0, 0            @ original table, alternative table, mode
w_child: .word 0, 0, 0            @ original list, alternative list, stack offset of the index
.org 0x1e0
  .word six_tab_r0, six_tab_r1, six_bg, six_child, six_pmap, six_tab_r2
.org 0x200
t_bg:        .space 0x140         @ alternative background table
t_child:     .space 8             @ 0x340 alternative child list
t_child_o:   .space 8             @ 0x348 original child list (when it is built from code)
.org 0x380
t_sites:     .space 0x180         @ up to 31 sites x 12 bytes, ends with 0
t_more:      .space 0x300         @ 0x500: more alternative tables
.org 0x800
