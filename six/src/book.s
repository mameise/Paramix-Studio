/* Tropical/Glacier Tama File. This code is position independent within XIP.
 * The game supplies every screen and animation. The field selector swaps the
 * labels and NEW markers on its three existing rows when the cursor changes bank.
 */
.syntax unified
.thumb
.cpu cortex-m3
.section .text
.global _start

.equ SCENE,              0x180141d8
.equ SAVE,               0x18000be8
.equ BOOK_PRIMARY,       SAVE + 0xf80 - 4
.equ BOOK_VIEWED,        SAVE + 0xf8c - 4
.equ NEON_PRIMARY,       SAVE + 0xf98 - 4
.equ NEON_VIEWED,        SAVE + 0xfb0 - 4
.equ NATIVE_PRIMARY,     SAVE + 0x90
.equ NATIVE_VIEWED,      SAVE + 0xa0
.equ FIELD,              0x4cc
.equ PAGE_ARROW,         0x4ec  /* extra word at the end of the native scene */
.equ ARROW_SPRITE,       89     /* original UI arrows: up and down frames */

.macro callGame addr
  ldr r12, =\addr
  blx r12
.endm

.macro loadScene reg
  ldr \reg, =SCENE
  ldr \reg, [\reg]
.endm

/* Header offsets live here so the Studio can install the branches. */
_start:
  .ascii "BK06"
  .word 3
  .word getPrimary - _start, getViewed - _start, setPrimary - _start, setViewed - _start
  .word eligible - _start, mapField - _start, selectorWidget - _start
  .word wrapNext - _start, cursorTable - _start, initSelector - _start, moved - _start
  .word detailSprites - _start, detailTitle - _start, extraIds - _start
  .word listExtra - _start, fieldCount - _start
  .word neonNames - _start

/* Find the slot and bit for an added character. The IDs are populated from the
 * ROM's own character table in bit order, independently of the field row shown.
 * r0 stays untouched on fall-through to the game's original function.
 */
extraFlag:
  adr r1, extraIds
  movs r2, #0
  adr r3, fieldCount
  ldrb r3, [r3]
  cmp r3, #9
  ite eq
  moveq r12, #152
  movne r12, #66
1:
  ldrh r3, [r1], #2
  cmp r0, r3
  beq 4f
  adds r2, #1
  cmp r2, r12
  blo 1b
  movs r1, #0
  bx lr
4:
  cmp r2, #66
  bhs 5f
  movs r3, #22
  udiv r12, r2, r3
  mls r2, r12, r3, r2
  b 8f
5:
  subs r2, #66
  movs r3, #3
  cmp r2, #24
  blo 7f
  subs r2, #24
  adds r3, #2
  cmp r2, #38
  blo 7f
  subs r2, #38
  adds r3, #2
7:
  add.w r12, r3, r2, lsr #5
  and r2, #31
8:
  movs r1, #1
  lsls r1, r2
  mov r2, r12
  bx lr

/* Each stock prologue replaced by a branch is restored before tail-calling
 * the first retained instruction on a stock character.
 */
.macro flagEntry name, addr, storage, neon, op
\name:
  push {r4, lr}
  bl extraFlag
  pop {r4, lr}
  cbz r1, 2f
  cmp r2, #3
  ite lo
  ldrlo r3, =\storage
  ldrhs r3, =\neon - 12
  add.w r3, r3, r2, lsl #2
  ldr r2, [r3]
.ifc \op,set
  orrs r2, r1
  str r2, [r3]
.else
  tst r2, r1
  ite eq
.ifc \op,primary
  moveq r0, #0
  movne r0, #1
.else
  moveq r0, #1
  movne r0, #0
.endif
.endif
  bx lr
2:
  push {r7, lr}
  sub sp, #0x18
  ldr r12, =\addr
  bx r12
.endm

flagEntry getPrimary, 0x100056d9, BOOK_PRIMARY, NEON_PRIMARY, primary
flagEntry getViewed,  0x10005635, BOOK_VIEWED, NEON_VIEWED, viewed
flagEntry setPrimary, 0x10005811, BOOK_PRIMARY, NEON_PRIMARY, set
flagEntry setViewed,  0x1000577d, BOOK_VIEWED, NEON_VIEWED, set

eligible:
  push {r4, lr}
  bl extraFlag
  pop {r4, lr}
  cbz r1, 1f
  movs r0, #1
  bx lr
1:
  sub sp, #0x10
  strh.w r0, [sp, #0xc]
  ldr r12, =0x100182df
  bx r12

/* The native animation scan serves the first six ordinals. */
mapField:
  subs r0, #1
  cmp r0, #5
  bhi 1f
  adr r1, fieldOrdinals
  ldrb r0, [r1, r0]
  bx lr
1:
  movs r0, #255
  bx lr

selectorWidget:
  cmp r1, #3
  it hs
  subhs r1, #3
  cmp r1, #3
  it hs
  subhs r1, #3
  addw r1, r1, #0x236
  bx lr

/* r1 points at the game's scene pointer and is retained by the caller. */
wrapNext:
  adr r2, fieldCount
  ldrb r2, [r2]
  cmp r0, r2
  it hs
  movhs r0, #0
  bx lr

moved:
  push {r4, lr}
  callGame 0x10023f21
  pop {r4, lr}
  b refresh

initSelector:
  push {r4, lr}
  sub sp, #16
  loadScene r4
  movs r1, #0
  movs r2, #ARROW_SPRITE
  str r2, [sp]
  str r1, [sp, #4]
  str r1, [sp, #8]
  ldr r0, [r4, #0x18]
  movs r2, #118
  movs r3, #116
  /* Create a scene-owned sprite. */
  callGame 0x1006f7ad
  str r0, [r4, #PAGE_ARROW]
  add sp, #16
  pop {r4, lr}
  b refresh

/* Draw the existing widgets with labels from the game's language table.
 * The selected bank has three save words, one for each NEW indicator.
 */
refresh:
  push {r4-r10, lr}
  loadScene r4
  ldr r0, [r4, #FIELD]
  adr r5, fieldNames
  movs r9, #0
  ldr r6, =NATIVE_PRIMARY
  ldr r7, =NATIVE_VIEWED
  cmp r0, #3
  blo 1f
  adds r5, #6
  ldr r6, =BOOK_PRIMARY
  ldr r7, =BOOK_VIEWED
  cmp r0, #6
  blo 1f
  movs r9, #1
  adr r5, neonNames
  ldr r6, =NEON_PRIMARY
  ldr r7, =NEON_VIEWED
1:
  movs r8, #0
2:
  ldrh r0, [r5], #2
  callGame 0x10065e81
  cmp r9, #0
  beq 7f
  mov r1, r8
  bl fitNeonCaption
7:
  add.w r1, r4, r8, lsl #2
  ldr r1, [r1, #0x28]
  ldr r1, [r1, #0x128]
  str r0, [r1]                   /* native text setter 0x10064f94 */
  ldr r0, [r6], #4
  ldr r1, [r7], #4
  bics r0, r1
  cmp r9, #0
  bne 4f
  lsls r0, #10                   /* 22 entries in each older region */
  b 6f
4:
  ldr r2, [r6], #4
  ldr r1, [r7], #4
  cmp r8, #1
  beq 5f
  lsls r0, #8                    /* 24 entries in the first and last Neon region */
  b 6f
5:
  bics r2, r1
  ands r2, #0x3f                 /* 38 entries in the middle Neon region */
  orrs r0, r2
6:
  mov.w r1, #0
  it ne
  movne.w r1, #0x100
  add.w r0, r4, r8, lsl #2
  ldr r0, [r0, #0x44]
  adds r0, #0x9c
  callGame 0x100668e5
  adds r8, #1
  cmp r8, #3
  blo 2b
  ldr r5, [r4, #PAGE_ARROW]
  cbz r5, 3f
  ldr r0, [r4, #FIELD]
  adr r1, fieldCount
  ldrb r1, [r1]
  subs r1, #3
  cmp r0, r1
  ite lo
  movlo r6, #1                    /* down before the final bank, up there */
  movhs r6, #0
  mov r0, r5
  movs r1, #ARROW_SPRITE
  mov r2, r6
  movs r3, #0
  callGame 0x1000996d
  add.w r0, r5, #0x38
  movs r1, #13
  cmp r6, #0
  it ne
  movne r1, #116
  lsls r1, #16
  callGame 0x10066abd
3:
  pop {r4-r10, pc}

detailTitle:
  push {r4, lr}
  ldr r2, [r0, #FIELD]
  ldr r4, [r0, #0x28]
  ldr r4, [r4, #0x128]
  cmp r2, #3
  bhs 1f
  addw r0, r2, #262
  callGame 0x10065e81
  b 2f
1:
  cmp r2, #6
  blo 3f
  subs r2, #6
  adr r0, neonInitials
  b 4f
3:
  subs r2, #3
  adr r0, extraInitials
4:
  add.w r0, r0, r2, lsl #2
2:
  str r0, [r4]
  pop {r4, pc}

/* The stock labels use fixed-size glyphs. Keep a short localized name if it
 * fits the native row; use a compact Latin label for longer translations. */
fitNeonCaption:
  cbz r0, 3f
  mov r2, r0
  movs r3, #0
1:
  ldrh r12, [r2], #2
  cmp r12, #0
  beq 2f
  subw r12, r12, #305
  cmp r12, #165
  ite ls
  addls r3, #9
  addhi r3, #16
  cmp r3, #100
  bls 1b
3:
  adr r0, neonShortNames
  movs r2, #18
  mla r0, r1, r2, r0
2:
  bx lr

/* The old enumeration searches a fixed range of animation numbers and can
 * return at most one record per animation. New regions enumerate identities. */
listExtra:
  loadScene r1
  ldr r2, [r1, #FIELD]
  subs r2, #6
  cmp r2, #2
  bls 1f
  movs r0, #0x20
  str r0, [sp, #0x44]
  ldr r12, =0x1005eeb1
  bx r12
1:
  push {r4-r8, lr}
  adr r0, neonStarts
  ldrb r3, [r0, r2]
  adds r2, #1
  ldrb r6, [r0, r2]
  subs r6, r3
  adr r5, extraIds + 132
  add.w r5, r5, r3, lsl #1
  add.w r4, r1, #0x54
  movs r7, #0
2:
  ldrh r0, [r5], #2
  callGame 0x10023e11
  cbz r0, 3f
  ldrh r1, [r0, #0]
  ldrh r2, [r0, #2]
  ldrh r3, [r0, #4]
  strh r1, [r4, #0]
  strh r2, [r4, #2]
  strh r3, [r4, #4]
  adds r4, #6
  adds r7, #1
3:
  subs r6, #1
  bne 2b
  mov r0, r7
  pop {r4-r8, lr}
  strh.w r0, [sp, #0x4e]
  ldr r12, =0x1005ef5d
  bx r12

.align 2
fieldOrdinals: .byte 3, 4, 5, 2, 0, 1
fieldNames: .hword 13, 14, 15, 63, 64, 65
.align 2
neonNames: .hword 0, 0, 0
neonShortNames:
  .hword 320,332,336,330,326,320,0,0,0    /* COSMIC */
  .hword 331,322,340,305,337,318,330,318,0 /* NEW TAMA */
  .hword 337,318,330,318,0,0,0,0,0        /* TAMA */
neonStarts: .byte 0, 24, 62, 86
fieldCount: .byte 6
/* Native decorative IDs; the original three layout decorations repeat on the
 * added rows, where the native language table supplies the actual field names.
 */
.align 2
cursorTable: .hword 0x236, 0x237, 0x238, 0x236, 0x237, 0x238, 0x236, 0x237, 0x238
detailSprites: .hword 196, 198, 197, 199, 147, 186, 196, 198, 197, 199, 147, 186, 196, 198, 197, 199, 147, 186
extraInitials: .hword 329,0, 340,0, 336,0 /* L W S */
neonInitials: .hword 320,0, 331,0, 337,0  /* C N T */
.align 2
extraIds: .space 2 * (3 * 22 + 24 + 38 + 24)
.ltorg
