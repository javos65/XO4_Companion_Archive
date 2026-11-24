/* 3 x 5 pixel font for the Seeed Grove tri-colour e-ink screen.
 *
 * ASCII 0x20 .. 0x5F (space, digits, upper case letters, punctuation). Lower case letters are shown as
 * upper case (font table in eink_screen.c). One glyph is EINK_FONT_H bytes, one byte per pixel
 * row (top row first). Only the low 3 bits of a byte are used: bit 2 is the left pixel, bit 0 the right
 * pixel, 1 = ink. A character cell is EINK_FONT_ADV pixels wide (3 pixels glyph + 1 pixel gap), so
 * 16 characters take 64 pixels. */
#ifndef EINK_FONT3X5_H
#define EINK_FONT3X5_H

#include <stdint.h>

#define EINK_FONT_FIRST   0x20
#define EINK_FONT_LAST    0x5F
#define EINK_FONT_COUNT   (EINK_FONT_LAST - EINK_FONT_FIRST + 1)
#define EINK_FONT_W       3
#define EINK_FONT_H       5
#define EINK_FONT_ADV     4

extern const uint8_t eink_font3x5[EINK_FONT_COUNT * EINK_FONT_H];

#endif /* EINK_FONT3X5_H */
