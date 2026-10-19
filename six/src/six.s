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
        .word 6                   @ 0x04 version (4: plain values in the site list, 5: planet, 6: field menu)
        .word 0                   @ 0x08 flag address (RAM)
        .word 0xF04               @ 0x0C flag offset in the settings block
        .word 0, 0, 0             @ 0x10 sprites, composites, scripts before (for removing)
        .word 0                   @ 0x1C kind: 0 Tropical/Glacier, 1 Jade Forest
        .word 0                   @ 0x20 options: bit 0 planet of the field, bit 1 field menu
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

@ ---------------------------------------------------------------- field menu (version 6)
@ The game's own field menu (planet, zoom in once, A, "Fields") lists the field slots 0..2 of
@ the current set. These hooks make it list every field of both sets (from planet level 4) and
@ keep the Tama: the list holds the slot (as the firmware expects), the set of entry i is
@ bit 7 of fm_ent[i]. An adult (menu flag bit 0) gets the three slots of its own set.
@ Active when the count byte holds fm_n; otherwise the hooks do what the firmware did.
.org 0x800
w_fm:    .word 0                  @ 0x00 address of the menu object pointer
         .word 0                  @ 0x04 field byte (RAM)
         .word 0                  @ 0x08 planet level byte (RAM, low nibble)
         .word 0                  @ 0x0C original list builder (thumb)
         .word 0                  @ 0x10 original preview loader (thumb)
fm_off:  .byte 0, 0, 0, 0         @ 0x14 list, index, count, menu flag offsets in the object
fm_n:    .byte 0, 0, 0, 0         @ 0x18 entries
fm_ent:  .space 8                 @ 0x1C slot | set << 7
.org 0x830
  .word six_fbuild, six_fcmp, six_fset, six_fprev

@ r0 = list in the menu object. Replaces the call of the list builder.
.thumb_func
six_fbuild:
  push {r3-r7,lr}
  mov r4,r0
  ldr r5,=w_fm
  ldrb r6,[r5,#0x14]
  subs r6,r4,r6               @ object
  ldr r1,[r5,#8]
  ldrb r1,[r1]
  and r1,r1,#15
  ldrb r7,[r5,#0x18]
  cmp r1,#4
  blt 8f
  cmp r7,#2
  blt 8f
  ldr r1,[r5,#4]
  ldrsb r1,[r1]               @ field slot
  ldrb r2,[r5,#0x17]
  ldrb r2,[r6,r2]
  lsls r2,r2,#31
  beq 2f
  @ adult: slots 0..2 of the current set
  movs r0,#0
  strb r0,[r4]
  movs r0,#1
  strb r0,[r4,#1]
  movs r0,#2
  strb r0,[r4,#2]
  movs r7,#3
  mov r0,r1
  b 6f
2:ldr r2,=_start
  ldr r2,[r2,#8]
  ldrsb r2,[r2]               @ current set
  movs r3,#0                  @ i
  mov r12,#0xFF               @ exact match
  mov lr,#0xFF                @ same slot
3:cmp r3,r7
  bge 5f
  add r0,r5,r3
  ldrb r0,[r0,#0x1C]
  push {r0}
  and r0,r0,#3
  strb r0,[r4,r3]
  cmp r0,r1
  pop {r0}
  bne 4f
  cmp lr,#0xFF
  it eq
  moveq lr,r3
  lsrs r0,r0,#7
  cmp r0,r2
  bne 4f
  cmp r12,#0xFF
  it eq
  moveq r12,r3
4:adds r3,#1
  b 3b
5:mov r0,r12
  cmp r0,#0xFF
  it eq
  moveq r0,lr
  cmp r0,#0xFF
  it eq
  moveq r0,#0
6:ldrb r2,[r5,#0x15]
  strb r0,[r6,r2]             @ index
  ldrb r2,[r5,#0x16]
  strb r7,[r6,r2]             @ count
  pop {r3-r7,pc}
8:ldr r1,[r5,#0x0C]           @ as before: the firmware's list, then count and index moved
  blx r1
  ldrb r2,[r4,#3]
  ldrb r1,[r5,#0x16]
  strb r2,[r6,r1]
  ldrb r2,[r4,#4]
  ldrb r1,[r5,#0x15]
  strb r2,[r6,r1]
  pop {r3-r7,pc}

@ r0 = object + index, r1 = index. Returns the slot of the entry. The entries of the other set
@ return 0x7F, so the firmware never takes them for the current field.
.thumb_func
six_fcmp:
  push {r1-r3,lr}
  bl fm_get
  beq 1f
  ldr r2,=_start
  ldr r2,[r2,#8]
  ldrsb r2,[r2]
  cmp r3,r2
  it ne
  movne r0,#0x7F
1:pop {r1-r3,pc}

@ Same place before the firmware switches the field: switch the set first.
.thumb_func
six_fset:
  push {r1-r3,lr}
  bl fm_get
  beq 1f
  ldr r2,=_start
  ldr r2,[r2,#8]
  strb r3,[r2]
1:pop {r1-r3,pc}

@ r0 = object + index, r1 = index -> r0 = slot, r3 = set of the entry, Z set when inactive.
@ Uses r2. Keeps r1.
fm_get:
  ldr r3,=w_fm
  ldrb r2,[r3,#0x14]
  ldrsb r0,[r0,r2]
  push {r0}
  ldr r0,[r3]
  ldr r0,[r0]                 @ object
  ldrb r2,[r3,#0x16]
  ldrb r2,[r0,r2]             @ count
  ldrb r0,[r3,#0x18]
  cmp r2,r0
  bne 2f
  add r3,r3,r1
  ldrb r3,[r3,#0x1C]
  lsrs r3,r3,#7
  pop {r0}
  movs r2,#1
  cmp r2,#0                   @ Z clear: active
  bx lr
2:pop {r0}
  movs r2,#0
  cmp r2,#0                   @ Z set
  bx lr

@ Replaces the call of the preview loader (r0, r1 its arguments): the preview of an entry of
@ the other set loads with that set, then the set goes back.
.thumb_func
six_fprev:
  push {r3-r7,lr}
  mov r6,r0
  mov r7,r1
  ldr r4,=_start
  ldr r4,[r4,#8]
  ldrb r5,[r4]                @ set before
  ldr r3,=w_fm
  ldr r0,[r3]
  ldr r0,[r0]                 @ object
  ldrb r2,[r3,#0x16]
  ldrb r2,[r0,r2]
  ldrb r1,[r3,#0x18]
  cmp r2,r1
  bne 1f
  ldrb r2,[r3,#0x15]
  ldrsb r1,[r0,r2]            @ index
  add r1,r1,r3
  ldrb r1,[r1,#0x1C]
  lsrs r1,r1,#7
  strb r1,[r4]
1:mov r0,r6
  mov r1,r7
  ldr r3,=w_fm
  ldr r3,[r3,#0x10]
  blx r3
  strb r5,[r4]
  pop {r3-r7,pc}
.ltorg
