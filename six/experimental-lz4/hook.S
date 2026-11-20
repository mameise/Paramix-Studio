#ifndef LEGACY_CODEC
#define LEGACY_CODEC 0
#endif
#if LEGACY_CODEC
#define ARCHIVE_LIMIT 0x60d49000
#define CACHE_FIRST 0x60dee000
#define CACHE_LIMIT 0x60df2000
#define XOR_KEY 0x5c58
#else
#define ARCHIVE_LIMIT 0x60d29000
#define CACHE_FIRST 0x60dce000
#define CACHE_LIMIT 0x60dd2000
#define XOR_KEY 0x5d18
#endif

.syntax unified
.cpu cortex-m3
.thumb

/* Byte-RLE loader. Only consider tagged blocks from the archive or the native
 * active Tama cache. Land and Jade have one native stack word, so allocate
 * two words temporarily and restore SP before resuming the stock decoder. */
.section .text.hooks, "ax", %progbits
.thumb_func
.global lz4_enter
lz4_enter:
#if LEGACY_CODEC
    push {r4-r7, lr}
    sub sp, #12
#else
    push.w {r4-r8, lr}
    sub sp, #8
#endif
    mov.w r12, #0
    str.w r12, [sp, #4]
    movw r12, #0x1000
    movt r12, #0x6011
    cmp r1, r12
    blo 1f
    movw r12, #(ARCHIVE_LIMIT & 0xffff)
    movt r12, #(ARCHIVE_LIMIT >> 16)
    cmp r1, r12
    bhs cache_source
source_length:
    sub.w r12, r12, r1
    cmp r2, r12
    bhi 1f
    str r2, [sp, #4]
1:  b.w legacy_enter

#if LEGACY_CODEC
.org 0x38
    .word LEGACY_CODEC
#endif
.org 0x3C
    .ascii "PLZ2"
.org 0x40
.thumb_func
.global lz4_dispatch
lz4_dispatch:
    ldr r2, [sp, #4]
#if LEGACY_CODEC
    add sp, #8
#endif
    cbz r2, 2f
    push {r1, lr}
#if LEGACY_CODEC
    mov r1, r5
#endif
    mov r0, r4
    movw r3, #XOR_KEY
    movt r3, #0x1800
    ldrb r3, [r3]
    bl try_lz4
    pop.w {r1, lr}
    cbz r0, 2f
    b.w legacy_return
2:
#if LEGACY_CODEC
    movw r1, #XOR_KEY
#else
    movw r2, #XOR_KEY
#endif
    b.w legacy_dispatch

/* Growth and the native appearance generator copy complete sprite blocks into
 * flash. The close-up renderer reads them back without changing their codec. */
cache_source:
    movw r12, #(CACHE_FIRST & 0xffff)
    movt r12, #(CACHE_FIRST >> 16)
    cmp r1, r12
    blo 1b
    movw r12, #(CACHE_LIMIT & 0xffff)
    movt r12, #(CACHE_LIMIT >> 16)
    cmp r1, r12
    bhs 1b
    b source_length
