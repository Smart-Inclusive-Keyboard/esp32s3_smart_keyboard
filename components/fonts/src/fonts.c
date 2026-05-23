/*
 * Font lookup wrapper around the public-domain font8x8 table.
 * The table itself lives in font8x8_basic.h (kept verbatim from
 * the upstream source, http://github.com/dhepper/font8x8).
 */

#include "fonts.h"

#include "font8x8_basic.h"

static const uint8_t s_fallback[8] = {
    /* '?' if out of range */
    0x3C, 0x66, 0x60, 0x30, 0x18, 0x00, 0x18, 0x00,
};

const uint8_t *font_glyph_8x8(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < 0x20 || uc > 0x7E) {
        return s_fallback;
    }
    return (const uint8_t *)font8x8_basic[uc];
}
