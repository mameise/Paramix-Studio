/* Tropical/Glacier Tama File. This code is position independent within XIP.
 * The game supplies every screen and animation. The field selector swaps the
 * labels and NEW markers on its three existing rows when the cursor changes bank.
 */
.syntax unified
.thumb
.cpu cortex-m3
.section .text
.global _start

#include "book_profiles.inc"

.equ BOOK_PRIMARY,       SAVE + 0xf80 - 4
.equ BOOK_VIEWED,        SAVE + 0xf8c - 4
.equ NEON_PRIMARY,       SAVE + 0xf98 - 4
.equ NEON_VIEWED,        SAVE + 0xfb0 - 4
.equ NATIVE_PRIMARY,     SAVE + 0x90
.equ NATIVE_VIEWED,      SAVE + 0xa0

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
#if BOOK_PLATFORM
  .word 4
#else
  .word 3
#endif
  .word getPrimary - _start, getViewed - _start, setPrimary - _start, setViewed - _start
  .word eligible - _start, mapField - _start, selectorWidget - _start
  .word wrapNext - _start, cursorTable - _start, initSelector - _start, moved - _start
  .word detailSprites - _start, detailTitle - _start, extraIds - _start
  .word listExtra - _start, fieldCount - _start
  .word neonNames - _start
#if BOOK_PLATFORM
  .word nativeRosters - _start
#endif

/* Find the slot and bit for an added character. The IDs are populated from the
 * ROM's own character table in bit order, independently of the field row shown.
 * r0 stays untouched on fall-through to the game's original function.
 */
extraFlag:
  adr r1, extraIds
  movs r2, #0
#if BOOK_PLATFORM
  mov r12, #86
#else
  adr r3, fieldCount
  ldrb r3, [r3]
  cmp r3, #9
  ite eq
  moveq r12, #152
  movne r12, #66
#endif
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
#if !BOOK_PLATFORM
  cmp r2, #66
  bhs 5f
  movs r3, #22
  udiv r12, r2, r3
  mls r2, r12, r3, r2
  b 8f
5:
  subs r2, #66
#endif
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
#if BOOK_PLATFORM == 1
  mov r4,r1
#endif
  bl extraFlag
#if BOOK_PLATFORM != 1
  pop {r4, lr}
#endif
  cbz r1, 2f
  cmp r2, #3
  ite lo
  ldrlo r3, =\storage
  ldrhs r3, =\neon - 12
  add.w r3, r3, r2, lsl #2
  ldr r2, [r3]
.ifc \op,set
#if BOOK_PLATFORM == 1
  cmp r4, #0
  ite eq
  biceq r2, r1
  orrne r2, r1
#else
  orrs r2, r1
#endif
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
#if BOOK_PLATFORM == 1
  pop {r4, pc}
#else
  bx lr
#endif
2:
#if BOOK_PLATFORM == 1
  mov r1,r4
  pop {r4,lr}
#endif
  push {r7, lr}
  sub sp, #STOCK_FLAG_STACK
  ldr r12, =\addr
  bx r12
.endm

flagEntry getPrimary, GET_PRIMARY, BOOK_PRIMARY, NEON_PRIMARY, primary
flagEntry getViewed,  GET_VIEWED, BOOK_VIEWED, NEON_VIEWED, viewed
flagEntry setPrimary, SET_PRIMARY, BOOK_PRIMARY, NEON_PRIMARY, set
flagEntry setViewed,  SET_VIEWED, BOOK_VIEWED, NEON_VIEWED, set

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
  addw r1, r1, #WIDGET_BASE
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
  callGame MOVED
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
  callGame CREATE_SPRITE
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
#if !BOOK_PLATFORM
  adds r5, #6
  ldr r6, =BOOK_PRIMARY
  ldr r7, =BOOK_VIEWED
  cmp r0, #6
  blo 1f
#endif
  movs r9, #1
  adr r5, neonNames
  ldr r6, =NEON_PRIMARY
  ldr r7, =NEON_VIEWED
1:
  movs r8, #0
2:
  ldrh r0, [r5], #2
  callGame GAME_TEXT
  cmp r9, #0
  beq 7f
  mov r1, r8
  bl fitNeonCaption
7:
  add.w r1, r4, r8, lsl #2
  ldr r1, [r1, #0x28]
  ldr r1, [r1, #0x128]
  str r0, [r1]
#if BOOK_PLATFORM
  cmp r9, #0
  bne 9f
  mov r0, r8
  bl nativeNew
  b 6f
9:
#endif
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
  callGame SET_VISIBLE
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
  callGame SET_SPRITE
  add.w r0, r5, #0x38
  movs r1, #13
  cmp r6, #0
  it ne
  movne r1, #116
  lsls r1, #16
  callGame SET_POSITION
3:
  pop {r4-r10, pc}

#if BOOK_PLATFORM
/* Native rows may use either character suffixes or roster indexes for
 * progress. Their original accessors interpret those bits. */
nativeNew:
  push {r4-r6, lr}
  adr r4, nativeRosters
  movs r1, #44
  mla r4, r0, r1, r4
  movs r5, #22
1:
  ldrh r6, [r4], #2
  mov r0, r6
  bl getPrimary
  cbz r0, 2f
  mov r0, r6
  bl getViewed
  cmp r0, #0
  bne 3f
2:
  subs r5, #1
  bne 1b
3:
  cmp r0, #0
  pop {r4-r6, pc}
#endif

detailTitle:
  push {r4, lr}
  ldr r2, [r0, #FIELD]
  ldr r4, [r0, #0x28]
  ldr r4, [r4, #0x128]
  cmp r2, #3
  bhs 1f
  addw r0, r2, #DETAIL_TITLE
  callGame GAME_TEXT
  b 2f
1:
#if BOOK_PLATFORM
  subs r2, #3
  adr r0, neonInitials
  b 4f
#else
  cmp r2, #6
  blo 3f
  subs r2, #6
  adr r0, neonInitials
  b 4f
3:
  subs r2, #3
  adr r0, extraInitials
#endif
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
#if BOOK_PLATFORM
  subs r2, #3
#else
  subs r2, #6
#endif
  cmp r2, #2
  bls 1f
  movs r0, #STOCK_SCAN
  str r0, [sp, #0x44]
  ldr r12, =STOCK_SCAN_BODY
  bx r12
1:
  push {r4-r8, lr}
  adr r0, neonStarts
  ldrb r3, [r0, r2]
  adds r2, #1
  ldrb r6, [r0, r2]
  subs r6, r3
#if BOOK_PLATFORM
  adr r5, extraIds
#else
  adr r5, extraIds + 132
#endif
  add.w r5, r5, r3, lsl #1
  add.w r4, r1, #0x54
  movs r7, #0
2:
  ldrh r0, [r5], #2
  callGame FIND_CHARACTER
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
  ldr r12, =STOCK_LIST_READY
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
cursorTable:
  .rept 3
  .hword WIDGET_BASE, WIDGET_BASE + 1, WIDGET_BASE + 2
  .endr
detailSprites:
#if BOOK_PLATFORM == 1
  .rept 2
  .hword 569, 570, 571
  .endr
#elif BOOK_PLATFORM == 2
  .rept 2
  .hword 140, 179, 141, 180, 143, 182
  .endr
#else
  .rept 3
  .hword 196, 198, 197, 199, 147, 186
  .endr
#endif
extraInitials: .hword 329,0, 340,0, 336,0 /* L W S */
neonInitials: .hword 320,0, 331,0, 337,0  /* C N T */
.align 2
#if BOOK_PLATFORM
extraIds: .space 2 * (24 + 38 + 24)
nativeRosters: .space 2 * 3 * 22
#else
extraIds: .space 2 * (3 * 22 + 24 + 38 + 24)
#endif
.ltorg
