.syntax unified
.cpu cortex-m3
.thumb

/* Tropical/Glacier byte-RLE loader. Its local stack word at +4 is unused.
 * Only consider tagged blocks from the archive or the native active Tama cache. */
.section .text.hooks, "ax", %progbits
.thumb_func
.global lz4_enter
lz4_enter:
    push.w {r4-r8, lr}
    sub sp, #8
    mov.w r12, #0
    str.w r12, [sp, #4]
    movw r12, #0x1000
    movt r12, #0x6011
    cmp r1, r12
    blo 1f
    movw r12, #0x9000
    movt r12, #0x60d2
    cmp r1, r12
    bhs cache_source
source_length:
    sub.w r12, r12, r1
    cmp r2, r12
    bhi 1f
    str r2, [sp, #4]
1:  b.w legacy_enter

.org 0x3C
    .ascii "PLZ2"
.org 0x40
.thumb_func
.global lz4_dispatch
lz4_dispatch:
    ldr r2, [sp, #4]
    cbz r2, 2f
    push {r1, lr}
    mov r0, r4
    movw r3, #0x5d18
    movt r3, #0x1800
    ldrb r3, [r3]
    bl try_lz4
    pop.w {r1, lr}
    cbz r0, 2f
    b.w legacy_return
2:  movw r2, #0x5d18
    b.w legacy_dispatch

/* Growth and the native appearance generator copy complete sprite blocks into
 * flash. The close-up renderer reads them back without changing their codec. */
cache_source:
    movw r12, #0xe000
    movt r12, #0x60dc
    cmp r1, r12
    blo 1b
    movw r12, #0x2000
    movt r12, #0x60dd
    cmp r1, r12
    bhs 1b
    b source_length
