#pragma once

/*
 * Tiny embedded bitmap fonts for the Smart Keyboard UI.
 *
 * We don't depend on LVGL, so the font format is deliberately
 * minimal: one byte per row, MSB = leftmost pixel. The base font
 * is the public-domain font8x8 by Daniel Hepper (an IBM PC VGA
 * derivative), covering ASCII 0x20..0x7E. Larger pixel sizes
 * (16, 24) are produced by integer-scaling each glyph at draw
 * time -- the source bitmap is the same.
 *
 * The display component is responsible for actually plotting
 * pixels; this file just hands out raw glyph rows.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_BASE_W 8
#define FONT_BASE_H 8

/* Returns the 8 row bytes for ASCII character c, or the glyph for
 * '?' if c is outside 0x20..0x7E. The returned pointer is valid
 * for the lifetime of the process. */
const uint8_t *font_glyph_8x8(char c);

/* True if bit (col, row) of the 8x8 glyph for c is set.
 * col in [0,7], row in [0,7]. */
static inline bool font_pixel_8x8(char c, int col, int row)
{
    const uint8_t *g = font_glyph_8x8(c);
    /* font8x8_basic stores each row LSB-first (bit 0 = leftmost). */
    return (g[row] >> col) & 1;
}

/* True if pixel (px, py) of the glyph for c, scaled to scale*8 px
 * square, is set. scale in [1, 8]. */
static inline bool font_pixel_scaled(char c, int scale, int px, int py)
{
    if (scale < 1) scale = 1;
    int col = px / scale;
    int row = py / scale;
    if (col < 0 || col >= FONT_BASE_W || row < 0 || row >= FONT_BASE_H) {
        return false;
    }
    return font_pixel_8x8(c, col, row);
}

#ifdef __cplusplus
}
#endif
