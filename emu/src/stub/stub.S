@ Stub in XIP flash: copies the app image to PRAM 0 and restarts.
@ APP_SRC (XIP address of the image) is set at build time.
@ With HOOKS: four entry points for the home screen handlers. They count how
@ long A and C are held together, start the emulator after LIMIT calls and
@ otherwise call the original handler.
  .syntax unified
  .thumb

@ Autostart (v1.2.8): with AUTO set, the stub decides once per boot (first home screen call)
@ whether to start the emulator right away. Mailbox word 0x20000FFC (kept over a restart and
@ the reset button, lost in the Paradise's deep sleep):
@   "PMXL"  written by the emulator when going back: stay in the Paradise
@   "PMXR"  written here: any later restart with it (reset button) starts the emulator
@   other   power-on or waking from deep sleep: the Paradise's seconds counter tells them
@           apart (it starts at 0 after power-on and when the clock is set)
@ Holding B skips the emulator. "Decided this boot" is kept in the UsageFault priority
@ (SHPR1 byte 2): it is cleared by every reset and the firmware does not use it.
@ AUTO is a placeholder the Studio sets to 0 or 1.
#ifndef AUTO
#define AUTO 0
#endif
  .macro AUTOSTART
#if defined(HOOKS)
  ldr r2, =AUTO
  cbz r2, 7f
  ldr r2, =0xE000ED18
  ldrb r3, [r2, #2]
  cbnz r3, 7f              @ already decided in this boot
  movs r3, #0xFF
  strb r3, [r2, #2]
  ldrb r3, [r2, #2]
  cbz r3, 7f               @ cannot keep the flag: never autostart
  ldr r2, =0x20000FFC
  ldr r3, [r2]
  ldr r0, =0x4C584D50      @ "PMXL": back from the emulator
  cmp r3, r0
  beq 8f
  ldr r0, =0x52584D50      @ "PMXR": restart while running
  cmp r3, r0
  beq 9f
  ldr r0, =0x45000304      @ mailbox lost: deep sleep (clock ran) or power-on (clock new)
  ldr r0, [r0]
  cmp r0, #30
  bhs 8f
9:
  ldr r0, =0x52584D50
  str r0, [r2]
  tst r1, #0x800           @ B (P0.11) held: stay in the Paradise
  beq 7f
  b start
8:
  ldr r0, =0x52584D50
  str r0, [r2]
7:
  ldr r0, =0x40018000
#endif
  .endm
@ 1.7.2 test19 (BFALL): C held when the Paradise comes up after switching on (or after the reset button) starts
@ the flash app of the write protection (0xFF0000, its load table at 0xFFE000), a way back when a program or a
@ save leaves the screen black. Once per boot (BusFault priority byte as the flag, the firmware does not use it);
@ not when back from a program on purpose ("PMXL") and not after waking from deep sleep (mailbox lost, clock ran).
#ifndef NO_BFALL
  .macro BFALL
  ldr r2, =0xE000ED19
  ldrb r3, [r2]
  cbnz r3, 7f
  movs r3, #0xFF
  strb r3, [r2]
  ldrb r3, [r2]
  cbz r3, 7f
  tst r1, #0x400           @ C (P0.10); B at switching on already skips the autostart
  bne 7f
#ifndef BFALL_WITH_A
@ 1.7.3 test29 (BFALL_NOA): not with A held too. A and C is the program key on the home screen; the first home
@ handler call of a boot with A+C held (within 30 s of switching on, or after a reset) started the flash app instead.
  tst r1, #0x200           @ A (P0.9) held: the menu key, no way back
  beq 7f
#endif
  ldr r2, =0x20000FFC
  ldr r2, [r2]
  ldr r0, =0x4C584D50
  cmp r2, r0
  beq 7f
  ldr r0, =0x45000304
  ldr r0, [r0]
  cmp r0, #30
  blo 6f
  ldr r0, =0x52584D50      @ "PMXR" / "PMXM": a reset, the mailbox was kept
  cmp r2, r0
  beq 6f
  ldr r0, =0x4D584D50
  cmp r2, r0
  bne 7f
6:ldr r0, =0x60FFE000      @ load table of the flash app, uncached flash
  ldr r2, [r0]
  ldr r3, =0x494E4F53      @ "SONI"
  cmp r2, r3
  bne 7f
  ldr r2, [r0, #0x10]
  ldr r3, =0x60FF0000
  cmp r2, r3
  bne 7f
  cpsid i
  mov r4, r3
  b realstart
7:
  ldr r0, =0x40018000
  ldr r1, [r0]
  .endm
#else
  .macro BFALL
  .endm
#endif
  .global start
  .thumb_func
start:
  cpsid i
#ifdef FELD
  ldr r4, app_src
  b.w realstart
  .align 2
app_src:  .word APP_SRC
@ 1.6.0: start the program image at r0 (XIP address), fixed at +0x0C. The menu starts its
@ other programs (more emulators) through here.
  .global start_src
  .thumb_func
start_src:
  cpsid i
  mov r4, r0
@ 1.6.6: the cached window (0x10000000) shows only 1 MB from 0x11000; programs further up (more emulators
@ from 0xD40000 down) came out as garbage. Copy from the uncached flash (0x60000000 + offset) instead.
  add r4, r4, #0x50000000
  add r4, r4, #0x11000
  b.w realstart
@ 1.6.8: launcher ROM ("only a start device for programs"): the Studio points the handlers of every
@ scene after the setup (home, events, care ...) here, so the menu starts instead of the Paradise game.
  .org 0x20
  .global launch
  .thumb_func
launch:
  cpsid i
  ldr r4, feld_src
  b.w realstart
  .align 2
feld_src: .word FELD_SRC
@ 1.6.8: entry for the start device's trampolines (r12 = the scene's original handler): the menu starts
@ only once the Paradise has a save (settings block 0xEFE000 stamped, date set), otherwise the original
@ handler runs, so a setup after "reset" can finish in the Paradise itself.
  .org 0x2C
  .global lcheck_e
  .thumb_func
lcheck_e:
  b.w lcheck
#else
  ldr r4, =APP_SRC
  b.w realstart
  .ltorg
#endif

#if defined(HOOKS) && !defined(FELD)
  .org 0x40                @ hooks fixed at 0x40 (Studio: HOOK_OFF), original handlers at 0x94
  .global hook0, hook1, hook2, hook3
  .thumb_func
hook0: ldr.w r12, orig0
  b check
  .thumb_func
hook1: ldr.w r12, orig1
  b check
  .thumb_func
hook2: ldr.w r12, orig2
  b check
  .thumb_func
hook3: ldr.w r12, orig3
  b check
  .org 0x94
orig0: .word ORIG0
orig1: .word ORIG1
orig2: .word ORIG2
orig3: .word ORIG3
  .thumb_func
check:
  push {r0-r3, r12, lr}
  ldr r0, =0x40018000      @ GPIO0: A = P0.9, C = P0.10, pressed = 0
  ldr r1, [r0]
  BFALL
  AUTOSTART
  ldr r2, =0x20000FF8      @ counter at the end of the mailbox
  ldr r3, [r2]
  tst r1, #0x600
  bne 5f
  lsrs r0, r3, #16
  movw r1, #0xE7A1
  cmp r0, r1
  beq 4f
  movw r3, #0
  movt r3, #0xE7A1
4:
  adds r3, #1
  str r3, [r2]
  uxth r0, r3
  cmp r0, #LIMIT
  bhs start
  b 6f
5:
  movs r3, #0
  str r3, [r2]
6:
  pop {r0-r3, r12, lr}
  bx r12
  .ltorg
#endif
#if defined(HOOKS) && defined(FELD)
@ With field switch: A+C starts the field switch (FELD_SRC), which in turn also
@ offers the program (APP_SRC). Return targets fixed at 0x94 as before.
  .org 0x40
  .global hook0, hook1, hook2, hook3
  .thumb_func
hook0: ldr.w r12, orig0
  b check
  .thumb_func
hook1: ldr.w r12, orig1
  b check
  .thumb_func
hook2: ldr.w r12, orig2
  b check
  .thumb_func
hook3: ldr.w r12, orig3
  b check
  .org 0x94
orig0: .word ORIG0
orig1: .word ORIG1
orig2: .word ORIG2
orig3: .word ORIG3
  .thumb_func
check:
  push {r0-r3, r12, lr}
  ldr r0, =0x40018000      @ GPIO0: A = P0.9, C = P0.10, pressed = 0
  ldr r1, [r0]
  BFALL
  AUTOSTART
  ldr r2, =0x20000FF8      @ counter at the end of the mailbox
  ldr r3, [r2]
#ifndef HOOK_TEST
  tst r1, #0x600
  bne 5f
#endif
  lsrs r0, r3, #16
  movw r1, #0xE7A1
  cmp r0, r1
  beq 4f
  movw r3, #0
  movt r3, #0xE7A1
4:
  adds r3, #1
  str r3, [r2]
  uxth r0, r3
  cmp r0, #LIMIT
  blo 6f
  ldr.w r4, feld_src
  cmp r4, #0
  beq start
  cpsid i
  b realstart
5:
  movs r3, #0
  str r3, [r2]
6:
  pop {r0-r3, r12, lr}
  bx r12
  .ltorg
#endif

#if defined(HOOKS) && defined(FELD)
  .thumb_func
lcheck:
  push {r0-r3, r12, lr}
  ldr r0, =0x60EFE000      @ settings block, uncached flash
  ldrh r1, [r0, #0]
  ldrh r2, [r0, #2]
  mvns r2, r2
  uxth r2, r2
  cmp r1, r2               @ stamp: CRC and its inverse
  bne 1f
  ldrh r1, [r0, #0x1C]     @ year 2020..2099
  movw r2, #2020
  cmp r1, r2
  blo 1f
  movw r2, #2099
  cmp r1, r2
  bhi 1f
  ldrh r1, [r0, #0x1E]     @ month 1..12
  subs r1, #1
  cmp r1, #12
  bhs 1f
  cpsid i
  ldr.w r4, feld_src
  b realstart
1:
  pop {r0-r3, r12, lr}
  bx r12
  .ltorg
#endif

@ Restart like the firmware itself (routine at flash 0x110568): first switch the
@ clock to source 4 (0x45000000 = 0x8041, 0x4500000C = 4, wait for 0x44), then
@ copy the image to PRAM 0, then SYSRESETREQ. v0.8 without this sequence froze
@ the real device after A+C (suspected: clock), the emulator did not show it.
  .thumb_func
realstart:
  cpsid i
  ldr r3, =0x45000000
  movw r2, #0x8041
  str r2, [r3]
  movs r2, #4
  str r2, [r3, #0xC]
  ldr r1, =0x400000
3:
  ldr r2, [r3, #0xC]
  cmp r2, #0x44
  beq 4f
  subs r1, #1
  bne 3b
4:
  mov r0, r4
  movs r1, #0
  ldr r2, =0x10000
1:
  ldr r3, [r0], #4
  str r3, [r1], #4
  subs r2, #4
  bne 1b
  dsb
  isb
  ldr r0, =0xE000ED08      @ VTOR = 0, as after a real reset
  movs r1, #0
  str r1, [r0]
  ldr r0, =0xE000ED0C      @ AIRCR: keep priority grouping, SYSRESETREQ
  ldr r1, [r0]
  and r1, r1, #0x700
  ldr r2, =0x05FA0004
  orrs r1, r2
  str r1, [r0]
  dsb
2:
  b 2b
  .ltorg
