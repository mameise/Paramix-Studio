#ifndef PARAMIX_LAB_GHOST_CODEC_H
#define PARAMIX_LAB_GHOST_CODEC_H

#include "../experimental-lz4/cell.h"

enum {
    LAB_GHOST_SIZE = 0x1b000,
    LAB_POSES = 0x600,
    LAB_SPRITES = 0x1d34,
    LAB_PARTS = 6,
    LAB_POSE_BYTES = 110,
    LAB_LAYER_BYTES = 22,
    LAB_CELL_LIMIT = 2048, // Largest 4bpp character tile is 64 by 64.
    LAB_XOR = 0x53,
};
enum { GHOST_INVALID = -1, GHOST_NATIVE, GHOST_CONVERTED };

struct GhostWindow {
    uint8_t *bytes;
    unsigned start, end;
    uint32_t sum;
};

static unsigned ghost_u16(const uint8_t *bytes)
{
    return bytes[0] | ((unsigned)bytes[1] << 8);
}

static uint32_t ghost_u32(const uint8_t *bytes)
{
    return ghost_u16(bytes) | ((uint32_t)ghost_u16(bytes + 2) << 16);
}

/* The first TCP chunk carries the checksum for every sprite, including those
 * outside that chunk. Later chunks only need the bytes in their own window. */
static void ghost_write(struct GhostWindow *window, unsigned at,
                        const uint8_t *bytes, unsigned length, unsigned key)
{
    if (!window->start) {
        for (unsigned i = 0; i < length; i++)
            window->sum += (uint32_t)(bytes[i] ^ key) << (8 * ((at + i) & 3));
    }
    unsigned begin = at < window->start ? window->start : at;
    unsigned end = at + length < window->end ? at + length : window->end;
    for (unsigned i = begin; i < end; i++)
        window->bytes[i - window->start] = bytes[i - at] ^ key;
}

static void ghost_word(struct GhostWindow *window, unsigned at, uint32_t value)
{
    uint8_t word[] = {value, value >> 8, value >> 16, value >> 24};
    ghost_write(window, at, word, sizeof(word), 0);
}

static unsigned ghost_layer_id(const uint8_t *layer, unsigned pose)
{
    unsigned id = ghost_u16(layer);
    if (id < LAB_PARTS || !ghost_u16(layer + 6)) return id;
    // These shared effects are indexed in the receiver's Neon sprite archive.
    unsigned type = ghost_u16(layer + 20);
    if (type == 16) return 222;
    if (type == 17) return pose < 30 ? 224 : 223;
    return id;
}

/* A native byte-RLE block, or zero when the raw tile would be smaller. */
static unsigned ghost_rle(uint8_t *out, const uint8_t *raw, unsigned length)
{
    unsigned written = 0, pos = 0;
    while (pos < length) {
        unsigned run = 1;
        while (run < 127 && run < length - pos && raw[pos] == raw[pos + run]) run++;
        if (run >= 3) {
            if (written + 2 >= length) return 0;
            out[written++] = run;
            out[written++] = raw[pos];
            pos += run;
        } else {
            unsigned start = pos++;
            while (pos < length && pos - start < 127) {
                if (length - pos >= 3 && raw[pos] == raw[pos + 1] && raw[pos] == raw[pos + 2]) break;
                pos++;
            }
            unsigned count = pos - start;
            if (written + 1 + count >= length) return 0;
            out[written++] = 0x80 | count;
            for (unsigned i = start; i < pos; i++) out[written++] = raw[i];
        }
    }
    if (written + 1 >= length) return 0;
    out[written++] = 0;
    return written;
}

struct GhostPart {
    const uint8_t *bytes;
    unsigned size, tiles, pixels, raw_size;
    int tagged;
};

static int ghost_part(struct GhostPart *part)
{
    const uint8_t *sprite = part->bytes;
    // The legacy compatibility wrapper is only valid in XOR byte-RLE cells.
    if ((sprite[4] & 0xe8) != 0xa0) return 1;
    part->tiles = ghost_u16(sprite + 6);
    part->pixels = ghost_u16(sprite + 20);
    unsigned table_end = part->pixels + 8 * part->tiles;
    unsigned limit = ghost_u32(sprite);
    // Compact archives can retain a larger original size in the sprite header.
    if (limit > part->size) limit = part->size;
    if (part->pixels < 24 || table_end > limit) return 0;
    for (unsigned i = 0; i < part->tiles; i++) {
        const uint8_t *tile = sprite + part->pixels + 8 * i;
        uint32_t pointer = ghost_u32(tile);
        unsigned offset = pointer & 0x7fffffffu, size = ghost_u32(tile + 4);
        if (offset < 8 * part->tiles || offset > limit - part->pixels ||
            size > limit - part->pixels - offset) return 0;
        if (pointer >> 31) continue;
        unsigned tagged = legacy_lz4_size(sprite + part->pixels + offset, size, LAB_XOR);
        if (!tagged) continue;
        part->raw_size = ((sprite[8] * sprite[9] + 7) / 8) * 4;
        if (sprite[5] != 2 || tagged != part->raw_size || tagged > LAB_CELL_LIMIT) return 0;
        part->tagged = 1;
    }
    return 1;
}

static unsigned ghost_encode_part(struct GhostWindow *window, const struct GhostPart *part,
                                  unsigned start)
{
    const uint8_t *sprite = part->bytes;
    unsigned cursor = part->pixels + 8 * part->tiles;
    if (cursor > LAB_GHOST_SIZE - start) return 0;
    ghost_write(window, start + 4, sprite + 4, part->pixels - 4, 0);
    for (unsigned i = 0; i < part->tiles; i++) {
        const uint8_t *tile = sprite + part->pixels + 8 * i;
        unsigned pointer = ghost_u32(tile), size = ghost_u32(tile + 4);
        const uint8_t *data = sprite + part->pixels + (pointer & 0x7fffffffu);
        unsigned raw_flag = pointer & 0x80000000u, key = 0;
        uint8_t raw[LAB_CELL_LIMIT], encoded[LAB_CELL_LIMIT];
        if (!raw_flag && legacy_lz4_size(data, size, LAB_XOR)) {
            if (!legacy_lz4_decode(raw, data, size, part->raw_size, LAB_XOR)) return 0;
            size = ghost_rle(encoded, raw, part->raw_size);
            if (size) data = encoded;
            else {
                data = raw;
                size = part->raw_size;
                raw_flag = 0x80000000u;
            }
            key = LAB_XOR;
        }
        cursor = (cursor + 3) & ~3u;
        if (cursor > LAB_GHOST_SIZE - start || size > LAB_GHOST_SIZE - start - cursor) return 0;
        unsigned entry = start + part->pixels + 8 * i;
        ghost_word(window, entry, (cursor - part->pixels) | raw_flag);
        ghost_word(window, entry + 4, size);
        ghost_write(window, start + cursor, data, size, key);
        cursor += size;
    }
    ghost_word(window, start, cursor);
    return (cursor + 3) & ~3u;
}

/* Stream just one TCP window. The outgoing ghost in native flash is read-only.
 * An unmodified ghost uses the game's normal transfer callback. */
static int ghost_convert(const uint8_t *source, uint8_t *out, unsigned offset, unsigned length,
                         unsigned species, uint8_t *page_count)
{
    if (offset >= LAB_GHOST_SIZE || length > LAB_GHOST_SIZE - offset) return GHOST_INVALID;
    if (*page_count && offset >= (unsigned)*page_count * 4096) {
        for (unsigned i = 0; i < length; i++) out[i] = 0;
        return GHOST_CONVERTED;
    }
    struct GhostPart parts[LAB_PARTS];
    unsigned used = ghost_u32(source + 0x10c);
    int changed = 0;
    if (used < LAB_SPRITES || used > LAB_GHOST_SIZE) return GHOST_INVALID;
    for (unsigned i = 0; i < LAB_PARTS; i++) {
        unsigned start = ghost_u32(source + 0x110 + 8 * i);
        unsigned size = ghost_u32(source + 0x114 + 8 * i);
        parts[i].bytes = 0;
        parts[i].tagged = 0;
        if (!start && !size) continue;
        if ((start | size) & 3 || start < LAB_SPRITES || start > used ||
            size < 24 || size > used - start) return GHOST_INVALID;
        parts[i].bytes = source + start;
        parts[i].size = size;
        if (!ghost_part(&parts[i])) return GHOST_INVALID;
        changed |= parts[i].tagged;
    }
    for (unsigned at = LAB_POSES; !changed && at < LAB_SPRITES; at += LAB_LAYER_BYTES)
        changed = ghost_layer_id(source + at, (at - LAB_POSES) / LAB_POSE_BYTES) != ghost_u16(source + at);
    if (!changed) return GHOST_NATIVE;

    struct GhostWindow window = {out, offset, offset + length, 0};
    for (unsigned i = 0; i < length; i++) out[i] = 0;
    ghost_write(&window, 8, source + 8, 0x100, 0);
    uint32_t identity = ghost_u32(source + 0x108);
    if (species) identity = (identity & 0xffffu) | (species << 16);
    ghost_word(&window, 0x108, identity);
    ghost_write(&window, 0x140, source + 0x140, 0x40, 0);
    // Copy the entire pose area. Only the native pose records count toward the sum.
    uint32_t sum = window.sum;
    ghost_write(&window, 0x180, source + 0x180, LAB_POSES - 0x180, 0);
    window.sum = sum;
    for (unsigned at = LAB_POSES; at < LAB_SPRITES; at += LAB_LAYER_BYTES) {
        unsigned id = ghost_layer_id(source + at, (at - LAB_POSES) / LAB_POSE_BYTES);
        uint8_t index[] = {id, id >> 8};
        ghost_write(&window, at, index, sizeof(index), 0);
        ghost_write(&window, at + 2, source + at + 2, LAB_LAYER_BYTES - 2, 0);
    }
    unsigned cursor = LAB_SPRITES;
    for (unsigned i = 0; i < LAB_PARTS; i++) {
        const struct GhostPart *part = &parts[i];
        if (!part->bytes) continue;
        unsigned size = part->size;
        if (part->tagged) size = ghost_encode_part(&window, part, cursor);
        else if (size <= LAB_GHOST_SIZE - cursor) ghost_write(&window, cursor, part->bytes, size, 0);
        if (!size || size > LAB_GHOST_SIZE - cursor) return GHOST_INVALID;
        ghost_word(&window, 0x110 + 8 * i, cursor);
        ghost_word(&window, 0x114 + 8 * i, size);
        cursor += size;
    }
    ghost_word(&window, 0x10c, cursor);
    *page_count = (cursor + 4095) / 4096;
    if (!offset) {
        sum = window.sum;
        ghost_word(&window, 0, sum);
        ghost_word(&window, 4, 0u - sum);
    }
    return GHOST_CONVERTED;
}

#endif
