/* 5 x 7 pixel font for the Seeed Grove tri-colour e-ink screen. Same structure as eink_font3x5.h.
 *
 * ASCII 0x20 .. 0x5F (space, digits, upper case letters, punctuation). Lower case letters are shown as
 * upper case (font table in eink_screen.c). One glyph is EINK5X7_FONT_H bytes, one byte per pixel
 * row (top row first). Only the low 5 bits of a byte are used: bit 4 is the left pixel, bit 0 the right
 * pixel, 1 = ink. A character cell is EINK5X7_FONT_ADV pixels wide (5 pixels glyph + 1 pixel gap), so
 * 16 characters take 96 pixels (scale 1) and 12 characters fit at scale 2. */
#ifndef FONT_1BPP_5X7_H
#define FONT_1BPP_5X7_H

#include <stdint.h>

#define EINK5X7_FONT_FIRST   0x20
#define EINK5X7_FONT_LAST    0x5F
#define EINK5X7_FONT_COUNT   (EINK5X7_FONT_LAST - EINK5X7_FONT_FIRST + 1)
#define EINK5X7_FONT_W       5
#define EINK5X7_FONT_H       7
#define EINK5X7_FONT_ADV     6

extern const uint8_t eink_font5x7[EINK5X7_FONT_COUNT * EINK5X7_FONT_H];

#endif /* FONT_1BPP_5X7_H */
