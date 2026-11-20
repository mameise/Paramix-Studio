#include "cell.h"

/* Return zero for native RLE and one for a tagged block, even if invalid. */
unsigned try_lz4(uint8_t *out, const uint8_t *data, unsigned size, unsigned key)
{
    unsigned expected = legacy_lz4_size(data, size, key);
    if (!expected) return 0;
    uintptr_t address = (uintptr_t)out;
    if (expected > MAX_CELL_BYTES ||
        address < 0x18000000u || address > 0x18020000u - expected) return 1;
    if (!legacy_lz4_decode(out, data, size, expected, key))
        for (unsigned i = 0; i < expected; i++) out[i] = 0;
    return 1;
}
