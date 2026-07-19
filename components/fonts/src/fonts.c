/*
 * Font lookup wrapper around the embedded bitmap glyph tables.
 * All tables (8x8, 10x20, 12x16) are derived from Greybeard, a
 * vector / bitmap port of Uwe Waldmann's UW ttyp0 (MIT License):
 * https://github.com/flowchartsman/greybeard
 */

#include "fonts.h"

#include "font8x8_basic.h"
#include "font10x20_basic.h"
#include "font10x20_cyrillic.h"
#include "font12x16_cyrillic.h"

static const uint8_t s_fallback[8] = {
    /* '?' if out of range -- Greybeard gb-16 glyph downscaled to 8x8 */
    0x00, 0x7E, 0x42, 0x30, 0x08, 0x08, 0x00, 0x00,
};

static const uint8_t s_fallback_10x20[40] = {
    /* '?' if out of range -- Greybeard gb-18 glyph in the 10x20 cell */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3E, 0x00, 0x41, 0x00, 0x41, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x04, 0x00, 0x08, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t *font_glyph_8x8(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < 0x20 || uc > 0x7E) {
        return s_fallback;
    }
    return (const uint8_t *)font8x8_basic[uc];
}

const uint8_t *font_glyph_10x20(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < 0x20 || uc > 0x7E) {
        return s_fallback_10x20;
    }
    return font10x20_basic[uc - 0x20];
}

const uint8_t *font_glyph_10x20_cp(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) {
        return font10x20_basic[cp - 0x20];
    }
    for (int i = 0; i < FONT10X20_CYRILLIC_COUNT; ++i) {
        if (font10x20_cyrillic_cp[i] == cp) {
            return font10x20_cyrillic[i];
        }
    }
    return s_fallback_10x20;
}

/* '?' fallback rendered into the 12x16 cell (Greybeard gb-16). */
static const uint8_t s_fallback_12x16[32] = {
    0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x10, 0x80,
    0x10, 0x80, 0x00, 0x80, 0x01, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t *font_glyph_12x16_cp(uint32_t cp)
{
    for (int i = 0; i < FONT12X16_CYRILLIC_COUNT; ++i) {
        if (font12x16_cyrillic_cp[i] == cp) {
            return font12x16_cyrillic[i];
        }
    }
    return s_fallback_12x16;
}
