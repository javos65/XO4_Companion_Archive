/* 3 x 5 pixel font, see eink_font3x5.h. One line per glyph: five pixel rows, top row first.
 * Row bits: 4 = left pixel, 2 = middle pixel, 1 = right pixel. */
#include "eink_font3x5.h"

const uint8_t eink_font3x5[EINK_FONT_COUNT * EINK_FONT_H] = {
    0, 0, 0, 0, 0,   /* 0x20 space */
    2, 2, 2, 0, 2,   /* 0x21 exclamation mark */
    5, 5, 0, 0, 0,   /* 0x22 double quote */
    5, 7, 5, 7, 5,   /* 0x23 hash */
    3, 6, 2, 3, 6,   /* 0x24 dollar */
    5, 1, 2, 4, 5,   /* 0x25 percent */
    2, 5, 2, 5, 3,   /* 0x26 ampersand */
    2, 2, 0, 0, 0,   /* 0x27 apostrophe */
    1, 2, 2, 2, 1,   /* 0x28 open parenthesis */
    4, 2, 2, 2, 4,   /* 0x29 close parenthesis */
    0, 5, 2, 5, 0,   /* 0x2A asterisk */
    0, 2, 7, 2, 0,   /* 0x2B plus */
    0, 0, 0, 2, 4,   /* 0x2C comma */
    0, 0, 7, 0, 0,   /* 0x2D minus */
    0, 0, 0, 0, 2,   /* 0x2E dot */
    1, 1, 2, 4, 4,   /* 0x2F slash */
    7, 5, 5, 5, 7,   /* 0x30 digit 0 */
    2, 6, 2, 2, 7,   /* 0x31 digit 1 */
    7, 1, 7, 4, 7,   /* 0x32 digit 2 */
    7, 1, 7, 1, 7,   /* 0x33 digit 3 */
    5, 5, 7, 1, 1,   /* 0x34 digit 4 */
    7, 4, 7, 1, 7,   /* 0x35 digit 5 */
    7, 4, 7, 5, 7,   /* 0x36 digit 6 */
    7, 1, 2, 2, 2,   /* 0x37 digit 7 */
    7, 5, 7, 5, 7,   /* 0x38 digit 8 */
    7, 5, 7, 1, 7,   /* 0x39 digit 9 */
    0, 2, 0, 2, 0,   /* 0x3A colon */
    0, 2, 0, 2, 4,   /* 0x3B semicolon */
    1, 2, 4, 2, 1,   /* 0x3C less than */
    0, 7, 0, 7, 0,   /* 0x3D equals */
    4, 2, 1, 2, 4,   /* 0x3E greater than */
    7, 1, 3, 0, 2,   /* 0x3F question mark */
    2, 5, 7, 4, 3,   /* 0x40 at */
    2, 5, 7, 5, 5,   /* 0x41 letter A */
    6, 5, 6, 5, 6,   /* 0x42 letter B */
    3, 4, 4, 4, 3,   /* 0x43 letter C */
    6, 5, 5, 5, 6,   /* 0x44 letter D */
    7, 4, 6, 4, 7,   /* 0x45 letter E */
    7, 4, 6, 4, 4,   /* 0x46 letter F */
    3, 4, 5, 5, 3,   /* 0x47 letter G */
    5, 5, 7, 5, 5,   /* 0x48 letter H */
    7, 2, 2, 2, 7,   /* 0x49 letter I */
    1, 1, 1, 5, 2,   /* 0x4A letter J */
    5, 5, 6, 5, 5,   /* 0x4B letter K */
    4, 4, 4, 4, 7,   /* 0x4C letter L */
    5, 7, 7, 5, 5,   /* 0x4D letter M */
    6, 5, 5, 5, 5,   /* 0x4E letter N */
    2, 5, 5, 5, 2,   /* 0x4F letter O */
    6, 5, 6, 4, 4,   /* 0x50 letter P */
    2, 5, 5, 6, 3,   /* 0x51 letter Q */
    6, 5, 6, 5, 5,   /* 0x52 letter R */
    3, 4, 2, 1, 6,   /* 0x53 letter S */
    7, 2, 2, 2, 2,   /* 0x54 letter T */
    5, 5, 5, 5, 7,   /* 0x55 letter U */
    5, 5, 5, 5, 2,   /* 0x56 letter V */
    5, 5, 7, 7, 5,   /* 0x57 letter W */
    5, 5, 2, 5, 5,   /* 0x58 letter X */
    5, 5, 2, 2, 2,   /* 0x59 letter Y */
    7, 1, 2, 4, 7,   /* 0x5A letter Z */
    6, 4, 4, 4, 6,   /* 0x5B open bracket */
    4, 4, 2, 1, 1,   /* 0x5C backslash */
    3, 1, 1, 1, 3,   /* 0x5D close bracket */
    2, 5, 0, 0, 0,   /* 0x5E caret */
    0, 0, 0, 0, 7    /* 0x5F underscore */
};
