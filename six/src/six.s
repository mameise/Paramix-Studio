@ Paramix Studio: more fields for Tropical/Glacier (Land, Water, Sky added) and Jade Forest (Sky added).
@ One 4 KB page at flash 0x10F000 (XIP 0x100FE000). The Studio patches the firmware to call
@ the code here and fills in every address and table below. A set byte in the settings block
@ (offset 0xF04) says which tables the field slots use: 0 = the original ones, 1 = the
@ alternative ones (Land, Water, Sky on Tropical/Glacier, Sky in slot 1 on Jade Forest).
@ Version 7: a second byte (0xF05), written only by the Paramix menu when it starts a field,
@ picks the games of the lab: 0 = the firmware's own, 1 = Land/Water/Sky, 2 = Jade Forest.
.syntax unified
.thumb
.text
.global _start
_start:
hdr:    .word 0x31584953          @ 0x00 "SIX1"
        .word 7                   @ 0x04 version (4: plain values in the site list, 5: planet, 6: field menu, 7: games)
        .word 0                   @ 0x08 flag address (RAM)
        .word 0xF04               @ 0x0C flag offset in the settings block
        .word 0, 0, 0             @ 0x10 sprites, composites, scripts before (for removing)
        .word 0                   @ 0x1C kind: 0 Tropical/Glacier, 1 Jade Forest
        .word 0                   @ 0x20 options: bit 0 planet of the field, bit 1 field menu, bit 2 games per start field
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

@ ---------------------------------------------------------------- games per start field (version 7)
@ The Paramix menu writes the family byte (settings 0xF05) when it starts a field. In the scenes
@ listed at gm_sc the lab games then use other pictures: every changed place in the firmware
@ calls six_imm, which looks up the return address in the site list and puts the value of the
@ family into the register (for "add rd, rn, #imm" the value is added to rn). The lab card list
@ steps through the cards of the family (six_cnext, six_cprev).
.org 0x980
w_gm:    .word 0                  @ 0x00 site list: return address, value 0, 1, 2, rd, rn (0xFF none), flags (bit 0: any scene), 20 bytes, ends with 0
         .word 0                  @ 0x04 family byte (RAM)
         .word 0                  @ 0x08 scene variable (RAM: u16 now, u16 next)
gm_sc:   .space 12                @ 0x0C scenes where the family counts (bytes, 0 = end)
gm_seq:  .space 16                @ 0x18 lab cards in order for family 1 and 2 (8 bytes each, 0xFF = end)
.org 0x9B0
  .word six_imm, six_cnext, six_cprev

@ r0 = family in a listed scene, else 0. Uses r1-r3.
gm_fam:
  push {r4,lr}
  ldr r3,=w_gm
  ldr r0,[r3,#4]
  cbz r0,8f
  ldrb r0,[r0]
  cmp r0,#3
  bhs 8f
  cbz r0,9f
  ldr r1,[r3,#8]
  ldrh r2,[r1]
  ldrh r1,[r1,#2]
  adds r3,#0x0C
1:ldrb r4,[r3],#1
  cbz r4,8f
  cmp r4,r2
  beq 9f
  cmp r4,r1
  beq 9f
  b 1b
8:movs r0,#0
9:pop {r4,pc}

@ Replaces a movw/mov.w/addw (bl) or a movw/movt pair (bl + nop). Keeps every other register and the flags.
.thumb_func
six_imm:
  push {r0-r12}
  mrs r4,apsr
  mov r5,lr
  bl gm_fam
  ldr r3,=w_gm
  ldr r3,[r3]
1:ldr r1,[r3]
  cbz r1,9f
  cmp r1,r5
  beq 2f
  adds r3,#20
  b 1b
2:ldrb r1,[r3,#18]            @ bit 0: the family counts in every scene
  lsls r1,r1,#31
  beq 4f
  ldr r0,=w_gm
  ldr r0,[r0,#4]
  ldrb r0,[r0]
  cmp r0,#3
  it hs
  movhs r0,#0
4:add r1,r3,r0,lsl #2
  ldr r1,[r1,#4]
  ldrb r2,[r3,#17]
  cmp r2,#13
  bhs 3f
  ldr r2,[sp,r2,lsl #2]
  add r1,r1,r2
3:ldrb r2,[r3,#16]
  str r1,[sp,r2,lsl #2]
9:msr APSR_nzcvq,r4
  mov lr,r5
  pop {r0-r12}
  bx lr

@ r2 = menu object. Replaces "ldrh.w r1,[r2,#126]" before the card number goes up (+1) or down (-1):
@ r1 = next card - 1 or previous card + 1 in the order of the family (as before without a family).
.macro CARD dir
  push {r0,r2-r5,lr}
  mrs r4,apsr
  ldrh r5,[r2,#126]
  bl gm_fam
  mov r1,r5
  cbz r0,9f
  ldr r3,=gm_seq-8
  add r3,r3,r0,lsl #3
  movs r2,#0
1:ldrb r0,[r3,r2]
  cmp r0,#0xFF
  beq 9f
  cmp r0,r5
  beq 2f
  adds r2,#1
  cmp r2,#8
  blo 1b
  b 9f
.if \dir > 0
2:adds r2,#1
  ldrb r0,[r3,r2]
  cmp r2,#8
  it hs
  movhs r0,#0xFF
  cmp r0,#0xFF
  it eq
  ldrbeq r0,[r3]
  subs r1,r0,#1
.else
2:cbnz r2,3f
4:ldrb r0,[r3,r2]
  cmp r0,#0xFF
  beq 5f
  adds r2,#1
  cmp r2,#8
  blo 4b
5:
3:subs r2,#1
  ldrb r0,[r3,r2]
  adds r1,r0,#1
.endif
9:msr APSR_nzcvq,r4
  pop {r0,r2-r5,pc}
.endm
.thumb_func
six_cnext:
  CARD 1
.thumb_func
six_cprev:
  CARD -1
.ltorg

@ ---------------------------------------------------------------- DNA per field (option bit 3)
@ The firmware keeps a DNA history of 12 entries: the field slot the Tama lived in, one entry per
@ stretch of time. With the set byte a slot means two fields (Glacier and Water share slot 1),
@ so the history mixed them up. With this option an entry is the field itself: slot + 3 when the
@ set byte is on and the slot is one of the switched ones (mask; Tropical/Glacier all three, Jade
@ Forest slot 1). Every place that writes or reads the history goes through the code here.
@ The firmware also keeps some values per field slot (residents, time in the field and more,
@ the regions at dna_reg). When the game's field menu switches the set, they are swapped with
@ a stash in the settings block, so every field keeps its own.
@ The first call converts an old history once (the entries count for the current set) and clears
@ the stash; the marker byte says it is done.
.org 0xC00
w_dna:   .word 0                  @ 0x00 settings block (RAM)
         .hword 0, 0              @ 0x04 offset of the DNA count (entries follow), field byte
         .hword 0, 0              @ 0x08 stash, marker byte
         .hword 0                 @ 0x0C byte with per slot bits (0 = none)
         .byte 0, 0               @ 0x0E its mask, slots switched by the set byte (bits 0..2)
dna_reg: .space 24                @ 0x10 up to 6 regions (u16 offset, u16 length), length 0 ends
dna_ic:  .space 12                @ 0x28 picture per field 0..5
         .space 4
.org 0xC40
  .word dna_maj, dna_maj6, dna_icon, dna_majf, dna_f6_r0, dna_f6_r1_r0, dna_f6_r1_r1, dna_child, dna_hchild, dna_fset

@ r0 = slot 0..2 -> r0 = field (slot + 3 when the set byte is on and the slot switches). Uses r12.
s2f:
  cmp r0,#2
  bhi 1f
  FLAG
  beq 1f
  push {r1}
  ldr r1,=w_dna
  ldrb r1,[r1,#0x0F]
  lsr r1,r1,r0
  lsls r1,r1,#31
  it ne
  addne r0,#3
  pop {r1}
1:bx lr

@ Once per save: old entries count for the current set, the stash starts empty. Keeps all registers.
dna_init:
  push {r0-r4,lr}
  ldr r4,=w_dna
  ldr r0,[r4]
  ldrh r1,[r4,#0x0A]
  ldrb r2,[r0,r1]
  cmp r2,#0x5A
  beq 9f
  movs r2,#0x5A
  strb r2,[r0,r1]
  ldrh r1,[r4,#4]
  add r1,r1,r0
  ldrb r2,[r1],#1
  cmp r2,#12
  it hi
  movhi r2,#12
1:cbz r2,3f
  ldrb r0,[r1]
  bl s2f
  strb r0,[r1],#1
  subs r2,#1
  b 1b
3:ldr r0,[r4]
  ldrh r1,[r4,#8]
  add r1,r1,r0
  movs r2,#1                  @ the bits byte
  add r3,r4,#0x10
4:ldrh r0,[r3,#2]
  cbz r0,5f
  add r2,r2,r0
  adds r3,#4
  b 4b
5:movs r0,#0
6:strb r0,[r1],#1
  subs r2,#1
  bne 6b
9:pop {r0-r4,pc}

@ r0 = settings block -> r0 = field 0..5 (other values as they are). Uses r12.
f6:
  ldr r12,=w_dna
  ldrh r12,[r12,#6]
  ldrb r0,[r0,r12]
  b s2f

@ r6 = 3: per slot of the current set (an entry counts for slot s when it is the field of s now),
@ r6 = 6: per field. -> r0 = slot/field with at least 4 entries (the last one wins, as in the
@ firmware), else r6. Uses r1-r5, r12.
majc:
  push {lr}
  bl dna_init
  ldr r4,=w_dna
  ldr r0,[r4]
  ldrh r1,[r4,#4]
  add r1,r1,r0
  ldrb r2,[r1],#1
  cmp r2,#12
  it hi
  movhi r2,#12
  movs r3,#0
1:cbz r2,2f
  ldrb r5,[r1],#1
  mov r0,r5
  cmp r6,#3
  bne 8f
  movs r0,#0                  @ which slot has this field now?
7:push {r0}
  bl s2f
  cmp r0,r5
  pop {r0}
  beq 8f
  adds r0,#1
  cmp r0,#3
  blo 7b
  b 3f
8:cmp r0,r6
  bhs 3f
  lsls r0,r0,#2
  mov r12,#1
  lsl r12,r12,r0
  add r3,r3,r12
3:subs r2,#1
  b 1b
2:mov r0,r6
  movs r1,#0
4:cmp r1,r6
  bhs 5f
  lsls r2,r1,#2
  lsr r12,r3,r2
  and r12,r12,#15
  cmp r12,#4
  it hs
  movhs r0,r1
  adds r1,#1
  b 4b
5:pop {pc}

@ The firmware's majority (b.w): slot 0..2 with at least 4 entries of its current field, else 3.
.thumb_func
dna_maj:
  push {r4-r6,lr}
  movs r6,#3
  bl majc
  pop {r4-r6,pc}

@ Second copy, used for the DNA picture only: field 0..5, else 6 (the caller shows picture 2 then).
.thumb_func
dna_maj6:
  push {r4-r6,lr}
  movs r6,#6
  bl majc
  pop {r4-r6,pc}

@ Before the firmware removes the leading field from the history: field 0..5, else 0xFF.
.thumb_func
dna_majf:
  push {r4,lr}
  bl dna_maj
  cmp r0,#3
  bhs 1f
  bl s2f
  pop {r4,pc}
1:movs r0,#0xFF
  pop {r4,pc}

@ Picture of a field (b.w from the firmware's function, r0 = field).
.thumb_func
dna_icon:
  push {r1,lr}
  bl dna_init
  uxtb r0,r0
  cmp r0,#6
  it hs
  movhs r0,#0
  ldr r1,=dna_ic
  ldrh r0,[r1,r0,lsl #1]
  pop {r1,pc}

@ In place of "ldrb r0,[r0,#field]": the field 0..5.
.thumb_func
dna_f6_r0:
  push {lr}
  bl dna_init
  bl f6
  pop {pc}

@ In place of "ldrb r0,[r1,#field]".
.thumb_func
dna_f6_r1_r0:
  push {lr}
  mov r0,r1
  bl dna_init
  bl f6
  pop {pc}

@ In place of "ldrsb r1,[r1,#field]", r0 is kept.
.thumb_func
dna_f6_r1_r1:
  push {r0,lr}
  mov r0,r1
  bl dna_init
  bl f6
  mov r1,r0
  pop {r0,pc}

@ Child of the hatching (in place of six_child): the same child, and the index in the caller's
@ stack (written into the history right after) becomes the field.
.thumb_func
dna_child:
  push {r1,r2,lr}
  ldr r1,=w_child
  ldr r0,[r1,#8]
  add r2,sp,r0
  adds r2,#12
  ldr r0,[r2]
  FLAG
  ite ne
  ldrne r1,[r1,#4]
  ldreq r1,[r1]
  ldrh r1,[r1,r0,lsl #1]
  bl s2f
  str r0,[r2]
  mov r0,r1
  pop {r1,r2,pc}

@ Child from the first history entry (12 bytes of the firmware): r0 = child of that field.
.thumb_func
dna_hchild:
  push {r1-r3,lr}
  bl dna_init
  ldr r3,=w_dna
  ldr r0,[r3]
  ldrh r1,[r3,#4]
  add r0,r0,r1
  ldrb r0,[r0,#1]
  ldr r1,=w_child
  cmp r0,#3
  ite hs
  ldrhs r1,[r1,#4]
  ldrlo r1,[r1]
  it hs
  subhs r0,#3
  cmp r0,#2
  it hi
  movhi r0,#0
  ldrh r0,[r1,r0,lsl #1]
  pop {r1-r3,pc}

@ In place of six_fset: when the set changes, the per slot values change places with the stash.
.thumb_func
dna_fset:
  push {r1-r3,lr}
  bl fm_get
  beq 1f
  ldr r2,=_start
  ldr r2,[r2,#8]
  ldrb r1,[r2]
  cmp r1,r3
  beq 1f
  bl dna_init
  strb r3,[r2]
  bl dna_swap
1:pop {r1-r3,pc}

@ Live per slot values <-> stash. Keeps all registers.
dna_swap:
  push {r0-r6,lr}
  ldr r4,=w_dna
  ldr r0,[r4]
  ldrh r1,[r4,#8]
  add r1,r1,r0
  add r5,r4,#0x10
1:ldrh r2,[r5,#2]
  cbz r2,3f
  ldrh r3,[r5]
  add r3,r3,r0
2:ldrb r6,[r3]
  ldrb r12,[r1]
  strb r6,[r1],#1
  strb r12,[r3],#1
  subs r2,#1
  bne 2b
  adds r5,#4
  b 1b
3:ldrh r3,[r4,#0x0C]
  cbz r3,9f
  add r3,r3,r0
  ldrb r2,[r4,#0x0E]
  ldrb r5,[r3]
  ldrb r6,[r1]
  and r12,r5,r2
  bic r5,r5,r2
  and r6,r6,r2
  orr r5,r5,r6
  strb r5,[r3]
  strb r12,[r1]
9:pop {r0-r6,pc}
.ltorg
