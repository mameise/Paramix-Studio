/* Lab Invite sender for Paradise editions 1 through 6. */
#include <stdint.h>
#include "ghost_codec.h"

#define NATIVE(type, address) ((type)((address) | 1u))
#ifndef FIRMWARE
#define FIRMWARE 5
#endif

#if FIRMWARE == 1
#define PLAY_STATE        0x1801414cu
#define CONNECTION_STATE  0x18014148u
#define SAVE_SETTINGS     0x18000ba0u
#define LINK_STATE        0x18013fbcu
#define HOME_RETURN       0x18001be6u
#define STAGE_OFFSET      0x118u
#define RANK_OFFSET       0x78u
#define LINK_FLAGS        0x10d8u
#define RELEASE_ROW       4
#define PLAY_SCENE        0x71u
#define POLL_LIST         0x10027200u
#define ADVANCE_STATE     0x1005c164u
#define REQUEST_SCENE     0x10079430u
#define EXIT_STATE        0x1005c134u
#define ALLOCATE          0x10016358u
#define CREATE_PROMPT     0x10020098u
#define BUTTON_PRESSED    0x10004d98u
#define CLEAR_LAYER       0x10008688u
#define LINK_READY        0x10054fd4u
#define LINK_INIT         0x10054ec4u
#define LINK_SEND         0x10055094u
#define LINK_RESULT       0x10054f90u
#define PLAY_CALLBACK     0x10076420u
#define NOW               0x100793eau
#define ECHO_SCHEDULE     0x100553dcu
#define ECHO_SEND         0x10054e0cu
#define NEW_EGG           0x1000d918u
#elif FIRMWARE == 4
#define PLAY_STATE        0x1801414cu
#define CONNECTION_STATE  0x18014148u
#define SAVE_SETTINGS     0x18000ba0u
#define LINK_STATE        0x18013fbcu
#define HOME_RETURN       0x18001be6u
#define STAGE_OFFSET      0x11cu
#define RANK_OFFSET       0x78u
#define LINK_FLAGS        0x10d8u
#define RELEASE_ROW       4
#define PLAY_SCENE        0x74u
#define POLL_LIST         0x10029020u
#define ADVANCE_STATE     0x1005f5d0u
#define REQUEST_SCENE     0x1007d948u
#define EXIT_STATE        0x1005f5a0u
#define ALLOCATE          0x10016658u
#define CREATE_PROMPT     0x100213dcu
#define BUTTON_PRESSED    0x10004ea4u
#define CLEAR_LAYER       0x10008844u
#define LINK_READY        0x100582f4u
#define LINK_INIT         0x100581e4u
#define LINK_SEND         0x100583b4u
#define LINK_RESULT       0x100582b0u
#define PLAY_CALLBACK     0x1007a87cu
#define NOW               0x1007d902u
#define ECHO_SCHEDULE     0x100586fcu
#define ECHO_SEND         0x1005812cu
#define NEW_EGG           0x1000db80u
#elif FIRMWARE == 5
#define PLAY_STATE        0x18014214u
#define CONNECTION_STATE  0x18014210u
#define SAVE_SETTINGS     0x18000be8u
#define LINK_STATE        0x1801407cu
#define HOME_RETURN       0x18001c6au
#define STAGE_OFFSET      0x128u
#define RANK_OFFSET       0x84u
#define LINK_FLAGS        0x12dcu
#define RELEASE_ROW       5
#define PLAY_SCENE        0x78u
#define POLL_LIST         0x1002d654u
#define ADVANCE_STATE     0x1006956cu
#define REQUEST_SCENE     0x100028ccu
#define EXIT_STATE        0x1006953cu
#define ALLOCATE          0x10018f28u
#define CREATE_PROMPT     0x1002505cu
#define BUTTON_PRESSED    0x10005ea8u
#define CLEAR_LAYER       0x1000a084u
#define LINK_READY        0x100609c8u
#define LINK_INIT         0x10060888u
#define LINK_SEND         0x10060c70u
#define LINK_RESULT       0x10060984u
#define PLAY_CALLBACK     0x1008d7b0u
#define NOW               0x10018e2cu
#define ECHO_SCHEDULE     0x10061070u
#define ECHO_SEND         0x100607a4u
#define NEW_EGG           0x1000f708u
#else
#error Unsupported firmware
#endif

#define SCENE (*(uint8_t *volatile *)PLAY_STATE)
#define CONNECTION (*(uint8_t *volatile *)CONNECTION_STATE)
#define SETTINGS ((volatile uint8_t *)SAVE_SETTINGS)
#define LINK (*(uint8_t *volatile *)LINK_STATE)
#define RELEASE_REQUEST ((uint8_t *)1u)
#define HOOK __attribute__((section(".text.entry")))

/* The ROM installer fills these 16-bit text IDs after rebuilding the text archive. */
#ifndef RELEASE_CONFIRM_TEXT
static inline unsigned confirmation_text(void) {
    unsigned value;
    __asm__ volatile ("movw %0, #0x6fe1" : "=r"(value));
    return value;
}
#define RELEASE_CONFIRM_TEXT confirmation_text()
#endif

#ifndef RELEASE_READY_TEXT
static inline unsigned ready_text(void) {
    unsigned value;
    __asm__ volatile ("movw %0, #0x6fe2" : "=r"(value));
    return value;
}
#define RELEASE_READY_TEXT ready_text()
#endif

struct ReleaseTransfer {
    uint32_t next_probe;
    uint32_t response_due;
    uint8_t phase;
    uint8_t acknowledged;
};

enum { BODY, HEARTBEAT, RESPONSE, TRAILER_DELAY, TRAILER, FAILED };

enum {
    ECHO_PENDING = 0x10,
    HEARTBEAT_COUNT = 6,
    HEARTBEAT_INTERVAL_MS = 2100,
    HEARTBEAT_TIMEOUT_MS = 5000,
    TRAILER_DELAY_MS = 1500,
};

HOOK int connection_selection(void *menu, int16_t *choice) {
    int selected = NATIVE(int (*)(void *, int16_t *), POLL_LIST)(menu, choice);
    if (!selected || !choice || *choice != RELEASE_ROW) return selected;
    if (((SETTINGS[STAGE_OFFSET] >> 2) & 7) != 5) {
        NATIVE(void (*)(void *, unsigned), ADVANCE_STATE)(CONNECTION, 1000);
        return 0;
    }
    // The Playdate scene owns this pointer and replaces it before its first update.
    SCENE = RELEASE_REQUEST;
    NATIVE(void (*)(unsigned), REQUEST_SCENE)(PLAY_SCENE);
    NATIVE(void (*)(void *, unsigned), EXIT_STATE)(CONNECTION, 0x7fe0);
    return 0;
}

HOOK void *allocate_scene(unsigned clear_size, unsigned pool) {
    unsigned release = SCENE == RELEASE_REQUEST;
    uint8_t *scene = NATIVE(void *(*)(unsigned, unsigned), ALLOCATE)(clear_size + 4, pool);
    if (!scene) return 0;
    scene[clear_size + 1] = 0;
    scene[clear_size + 2] = 0;  // Number of pages used by the converted ghost.
    scene[clear_size] = release;  // Outside the range initialized by the caller.
    return scene;
}

HOOK void *release_dialog(void *list, void *item, unsigned mode, unsigned flags, unsigned text) {
    if (SCENE[0x9c]) text = RELEASE_CONFIRM_TEXT;
    return NATIVE(void *(*)(void *, void *, unsigned, unsigned, unsigned), CREATE_PROMPT)
        (list, item, mode, flags, text);
}

HOOK int release_confirmation(unsigned key) {
    int pressed = NATIVE(int (*)(unsigned), BUTTON_PRESSED)(key);
    uint8_t *scene = SCENE;
    if (!pressed || !scene[0x9c] || scene[0x9d]) return pressed;
    void *list = *(void **)(scene + 0x18);
    NATIVE(void (*)(void *), CLEAR_LAYER)(list);
    *(void **)(scene + 0x20) =
        NATIVE(void *(*)(void *, void *, unsigned, unsigned, unsigned), CREATE_PROMPT)
            (list, 0, 0, 0, RELEASE_READY_TEXT);
    scene[0x9d] = 1;
    return 0;
}

HOOK int release_link_ready(void) {
    return !SCENE[0x9c] && NATIVE(int (*)(void), LINK_READY)();
}

struct LinkChunk {
    uint32_t flags;
    uint8_t *bytes;
    uint32_t length;
    uint32_t offset;
};

static unsigned release_callback(unsigned event, struct LinkChunk *chunk) {
    uint8_t *scene = SCENE;
    if (event == 1 && scene[0x40] == 1) {
        unsigned species = FIRMWARE == 1 ? *(volatile const uint16_t *)(SETTINGS + 0x104) : 0;
        int result = ghost_convert((const uint8_t *)0x60e66000u, chunk->bytes, chunk->offset,
                                   chunk->length, species, scene + 0x9e);
        if (result != GHOST_NATIVE) return result == GHOST_CONVERTED ? 0 : 3;
    }
    return NATIVE(unsigned (*)(unsigned, struct LinkChunk *), PLAY_CALLBACK)(event, chunk);
}

HOOK int initialize_link(unsigned mode, void *callback) {
    // The old sender reserves the upper mode bit for the newer envelope.
    if (SCENE[0x9c]) {
        mode = FIRMWARE == 5 ? 6 : 0x26;
        callback = release_callback;
    }
    return NATIVE(int (*)(unsigned, void *), LINK_INIT)(mode, callback);
}

#if FIRMWARE != 5
// Replay the replaced instruction, then add the newer envelope bit for Lab Invite.
HOOK __attribute__((naked)) void release_packet_type(void) {
    __asm__ volatile (
        "orr.w r1, r1, r3, lsl #3\n"
        "push {r3, r4}\n"
        "ldr r3, [r0]\n"
        "movw r4, #0x10ac\n"
        "ldrb r3, [r3, r4]\n"
        "and.w r3, r3, #0x20\n"
        "orr.w r1, r1, r3\n"
        "pop {r3, r4}\n"
        "bx lr\n");
}
#endif

#if FIRMWARE == 1
// The first firmware omits the species ID from the ghost header.
HOOK void release_ghost_chunk(uint32_t address, void *destination, unsigned length) {
    NATIVE(void (*)(uint32_t, void *, unsigned), 0x100787b4)(address, destination, length);
    if (address != 0x60e66000u || length < 0x10cu || !SCENE[0x9c]) return;
    uint32_t *header = destination;
    const unsigned field = 0x108 / sizeof(*header);
    uint32_t old = header[field];
    uint32_t species = *(volatile const uint16_t *)(SETTINGS + 0x104);
    uint32_t updated = (old & 0xffffu) | (species << 16);
    header[field] = updated;
    // The original checksum includes the species word. All other words are identical.
    header[0] += updated - old;
    header[1] = 0u - header[0];
}
#endif

HOOK int send_body(unsigned length) {
    if (SCENE[0x9c]) length = LAB_GHOST_SIZE;
    return NATIVE(int (*)(unsigned), LINK_SEND)(length);
}

HOOK int status_after_body(uint16_t *status) {
    // The patch allocates these bytes immediately after the original scene.
    struct ReleaseTransfer *t = (void *)(SCENE + 0x90);
    int (*const completed)(uint16_t *) = NATIVE(int (*)(uint16_t *), LINK_RESULT);
    if (!SCENE[0x9c]) return completed(status);
    uint32_t now = NATIVE(uint32_t (*)(void), NOW)();
    uint8_t *link = LINK;
    if (!link) {
        *status = 1;
        return 1;
    }
    volatile uint8_t *flags = link + LINK_FLAGS;
    if (t->phase == BODY) {
        if (!completed(status)) return 0;
        if (*status) return 1;
        // The original link scheduler is superseded by the counted replies below.
        NATIVE(void (*)(unsigned), ECHO_SCHEDULE)(0);
        t->acknowledged = 0;
        t->next_probe = now + HEARTBEAT_INTERVAL_MS;
        t->phase = HEARTBEAT;
        return 0;
    }
    if (t->phase == HEARTBEAT) {
        if ((int32_t)(now - t->next_probe) < 0) return 0;
        *flags |= ECHO_PENDING;
        NATIVE(void (*)(void), ECHO_SEND)();
        t->response_due = now + HEARTBEAT_TIMEOUT_MS;
        t->next_probe = now + HEARTBEAT_INTERVAL_MS;
        t->phase = RESPONSE;
        return 0;
    }
    if (t->phase == RESPONSE) {
        if (*flags & ECHO_PENDING) {
            if ((int32_t)(now - t->response_due) >= 0) {
                *flags &= ~ECHO_PENDING;
                t->phase = FAILED;
            }
            return 0;
        }
        if (++t->acknowledged < HEARTBEAT_COUNT) {
            t->phase = HEARTBEAT;
            return 0;
        }
        t->next_probe = now + TRAILER_DELAY_MS;
        t->phase = TRAILER_DELAY;
        return 0;
    }
    if (t->phase == TRAILER_DELAY) {
        if ((int32_t)(now - t->next_probe) < 0) return 0;
        // Payload selector 2 uses the native Playdate callback for a two-byte result.
        SCENE[0x40] = 2;
        *(uint16_t *)(SCENE + 0x54) = 0;
        if (!NATIVE(int (*)(unsigned), LINK_SEND)(2)) {
            t->phase = FAILED;
            return 0;
        }
        t->phase = TRAILER;
        return 0;
    }
    if (t->phase == TRAILER) {
        if (!completed(status)) return 0;
        if (*status) return 1;
        // Match the old firmware's normal new-egg return path.
        uint8_t level = SETTINGS[RANK_OFFSET] & 0x0f;
        if (level == 3 || (level >= 5 && level <= 9)) {
            ++level;
            // Keep the unlocked rank and return straight to the field.
            SETTINGS[RANK_OFFSET] = (level << 4) | level;
        }
        NATIVE(void (*)(unsigned), NEW_EGG)(0);
        volatile uint8_t *home_return = (volatile uint8_t *)HOME_RETURN;
        *home_return = (*home_return & 0xfc) | 1;
        NATIVE(void (*)(unsigned), REQUEST_SCENE)(28);
        NATIVE(void (*)(void *, unsigned), EXIT_STATE)(SCENE, 0x7fe0);
        return 0;
    }
    *status = 1;
    return 1;
}
