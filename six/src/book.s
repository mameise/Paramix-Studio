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
  .word 2
  .word getPrimary - _start, getViewed - _start, setPrimary - _start, setViewed - _start
  .word eligible - _start, mapField - _start, selectorWidget - _start
  .word wrapNext - _start, cursorTable - _start, initSelector - _start, moved - _start
  .word detailSprites - _start, detailTitle - _start, extraIds - _start

/* Find the slot and bit for an added character. The IDs are populated from the
 * ROM's own character table in bit order, independently of the field row shown.
 * r0 stays untouched on fall-through to the game's original function.
 */
extraFlag:
  adr r1, extraIds
  movs r2, #0
  movs r12, #3
1:
  ldrh r3, [r1], #2
  cmp r0, r3
  beq 4f
  adds r2, #1
  cmp r2, #22
  bne 1b
  movs r2, #0
  subs r12, #1
  bne 1b
  movs r1, #0
  bx lr
4:
  movs r1, #1
  lsls r1, r2
  rsb r2, r12, #3
  bx lr

/* Each stock prologue replaced by a branch is restored before tail-calling
 * the first retained instruction on a stock character.
 */
.macro flagEntry name, addr, storage, op
\name:
  push {r4, lr}
  bl extraFlag
  pop {r4, lr}
  cbz r1, 2f
  ldr r3, =\storage
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

flagEntry getPrimary, 0x100056d9, BOOK_PRIMARY, primary
flagEntry getViewed,  0x10005635, BOOK_VIEWED, viewed
flagEntry setPrimary, 0x10005811, BOOK_PRIMARY, set
flagEntry setViewed,  0x1000577d, BOOK_VIEWED, set

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

/* Character-table field type -> six book ordinals. */
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
  addw r1, r1, #0x236
  bx lr

/* r1 points at the game's scene pointer and is retained by the caller. */
wrapNext:
  cmp r0, #6
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
  push {r4-r8, lr}
  loadScene r4
  ldr r0, [r4, #FIELD]
  adr r5, fieldNames
  ldr r6, =NATIVE_PRIMARY
  ldr r7, =NATIVE_VIEWED
  cmp r0, #3
  blo 1f
  adds r5, #6
  ldr r6, =BOOK_PRIMARY
  ldr r7, =BOOK_VIEWED
1:
  movs r8, #0
2:
  ldrh r0, [r5], #2
  callGame 0x10065e81
  add.w r1, r4, r8, lsl #2
  ldr r1, [r1, #0x28]
  ldr r1, [r1, #0x128]
  str r0, [r1]                   /* native text setter 0x10064f94 */
  ldr r0, [r6], #4
  ldr r1, [r7], #4
  bics r0, r1
  lsls r0, #10                   /* ignore unused save bits */
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
  cmp r0, #3
  ite lo
  movlo r6, #1                    /* down on page 1, up on page 2 */
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
  pop {r4-r8, pc}

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
  subs r2, #3
  adr r0, extraInitials
  add.w r0, r0, r2, lsl #2
2:
  str r0, [r4]
  pop {r4, pc}

.align 2
fieldOrdinals: .byte 3, 4, 5, 2, 0, 1
fieldNames: .hword 13, 14, 15, 63, 64, 65
/* Native decorative IDs; the original three layout decorations repeat on the
 * added rows, where the native language table supplies the actual field names.
 */
.align 2
cursorTable: .hword 0x236, 0x237, 0x238, 0x236, 0x237, 0x238
detailSprites: .hword 196, 198, 197, 199, 147, 186, 196, 198, 197, 199, 147, 186
extraInitials: .hword 329,0, 340,0, 336,0 /* L W S */
.align 2
extraIds: .space 2 * 3 * 22
.ltorg
