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
  b.w realstart
#else
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
  ldr r4, =FELD_SRC
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
#ifdef FELD
  mov r0, r4
#else
  ldr r0, =APP_SRC
#endif
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
