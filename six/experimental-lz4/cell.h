#ifndef PARAMIX_LEGACY_LZ4_CELL_H
#define PARAMIX_LEGACY_LZ4_CELL_H
#include <stdint.h>
#define MAX_CELL_BYTES 0x4000u

static unsigned read_byte(const uint8_t *data, unsigned *cursor, unsigned key)
{
    return data[(*cursor)++] ^ key;
}

static unsigned read_length(const uint8_t *data, unsigned size,
                            unsigned *cursor, unsigned length, unsigned key)
{
    if (length == 15) {
        unsigned extra;
        do {
            if (*cursor >= size) return MAX_CELL_BYTES + 1;
            extra = read_byte(data, cursor, key);
            length += extra;
        } while (extra == 255 && length <= MAX_CELL_BYTES);
    }
    return length;
}

/* Zero denotes an untagged input. An oversized result denotes a bad header. */
static unsigned legacy_lz4_size(const uint8_t *data, unsigned size, unsigned key)
{
    static const uint8_t marker[] = {0x80, 0x80, 0x80, 0x80, 'P', 'L', 'Z', '4'};
    if (size < sizeof(marker)) return 0;
    for (unsigned i = 0; i < sizeof(marker); i++)
        if ((data[i] ^ key) != marker[i]) return 0;
    if (size < 11 || size > MAX_CELL_BYTES) return MAX_CELL_BYTES + 1;
    unsigned expected = (data[8] ^ key) | ((data[9] ^ key) << 8);
    return expected ? expected : MAX_CELL_BYTES + 1;
}

static int legacy_lz4_decode(uint8_t *out, const uint8_t *data, unsigned size,
                             unsigned expected, unsigned key)
{
    unsigned cursor = 10;
    unsigned written = 0;
    while (cursor < size) {
        unsigned token = read_byte(data, &cursor, key);
        unsigned literals = read_length(data, size, &cursor, token >> 4, key);
        if (literals > size - cursor || literals > expected - written) break;
        for (unsigned i = 0; i < literals; i++) out[written++] = read_byte(data, &cursor, key);
        if (cursor == size) return written == expected;
        if (size - cursor < 2) break;

        unsigned distance = read_byte(data, &cursor, key);
        distance |= read_byte(data, &cursor, key) << 8;
        unsigned match = read_length(data, size, &cursor, token & 15, key) + 4;
        if (!distance || distance > written || match > expected - written) break;
        for (unsigned i = 0; i < match; i++) {
            out[written] = out[written - distance];
            written++;
        }
    }

    return 0;
}
#endif
