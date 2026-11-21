/* Driver of the MKR_RGB_SPI block (custom:module:mkr_rgb_spi:1.0.0) for the
 * Arduino MKR RGB Shield: 84 APA102-2020 LEDs, 7 rows x 12 columns.
 *
 * The driver keeps a frame buffer in RAM (one RGB value per pixel plus one global
 * brightness). Drawing functions only change that buffer; mkr_rgb_show() copies it
 * to the block's pixel registers and starts the transfer (278 us at 10 MHz CKI).
 *
 * Coordinates: x = column 0..11 (left to right), y = row 0..6 (top to bottom).
 * Chain order on the shield (schematic ASX00010): row by row, each row left to right,
 * so LED index = y * 12 + x. If the board is mounted rotated, set the orientation
 * with mkr_rgb_set_orientation() instead of changing the application.
 */
#ifndef MKR_RGB_SPI_H
#define MKR_RGB_SPI_H

#include <stdint.h>
#include "mkr_rgb_spi_regs.h"

#define MKR_RGB_WIDTH             12u
#define MKR_RGB_HEIGHT            7u
#define MKR_RGB_NUM_LEDS          (MKR_RGB_WIDTH * MKR_RGB_HEIGHT)   /* 84 */

#define MKR_RGB_BRIGHTNESS_MAX    31u

/* orientation flags for mkr_rgb_set_orientation() */
#define MKR_RGB_FLIP_X            (1u << 0)   /* mirror left/right */
#define MKR_RGB_FLIP_Y            (1u << 1)   /* mirror top/bottom */

/* return codes */
#define MKR_RGB_OK                0
#define MKR_RGB_ERR_PARAM         1   /* null pointer or bad argument */
#define MKR_RGB_ERR_ID            2   /* ID register does not match: block not found */
#define MKR_RGB_ERR_SIZE          3   /* block has fewer LEDs than the matrix */
#define MKR_RGB_ERR_TIMEOUT       4   /* READY did not come back */

/* polls of CTRL before a wait for READY is declared timed out */
#define MKR_RGB_TIMEOUT_POLLS     100000u

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} mkr_rgb_rgb_t;

struct mkr_rgb_instance {
    const char  *instance_name;
    uint32_t     base_address;
    uint8_t      brightness;                         /* 0..31, applied to all pixels */
    uint8_t      orientation;                        /* MKR_RGB_FLIP_* */
    mkr_rgb_rgb_t fb[MKR_RGB_HEIGHT][MKR_RGB_WIDTH];    /* frame buffer [y][x] */
};

/* Binds the instance to the base address, checks ID and LED count, clears the frame
 * buffer and sets brightness 8. The LEDs are not updated until mkr_rgb_show().
 * Returns MKR_RGB_OK, MKR_RGB_ERR_PARAM, MKR_RGB_ERR_ID or MKR_RGB_ERR_SIZE. */
uint8_t mkr_rgb_init(struct mkr_rgb_instance *this_rgb, uint32_t base_addr);

/* 1 when the block is idle (no frame being sent). */
uint8_t mkr_rgb_is_ready(struct mkr_rgb_instance *this_rgb);

/* Global brightness 0..31 (values above 31 are clipped). */
void mkr_rgb_set_brightness(struct mkr_rgb_instance *this_rgb, uint8_t brightness);

/* Orientation of the matrix: combination of MKR_RGB_FLIP_X / MKR_RGB_FLIP_Y. */
void mkr_rgb_set_orientation(struct mkr_rgb_instance *this_rgb, uint8_t flags);

/* Frame buffer drawing; out-of-range coordinates are ignored. */
void mkr_rgb_clear(struct mkr_rgb_instance *this_rgb);
void mkr_rgb_fill(struct mkr_rgb_instance *this_rgb, uint8_t r, uint8_t g, uint8_t b);
void mkr_rgb_set_pixel(struct mkr_rgb_instance *this_rgb, uint8_t x, uint8_t y,
                      uint8_t r, uint8_t g, uint8_t b);

/* Copies a whole 7 x 12 image (img[y][x]) into the frame buffer. */
void mkr_rgb_write_image(struct mkr_rgb_instance *this_rgb,
                        const mkr_rgb_rgb_t img[MKR_RGB_HEIGHT][MKR_RGB_WIDTH]);

/* Waits for READY, writes the 84 pixel registers and starts the transfer.
 * Returns without waiting for the transfer to finish.
 * Returns MKR_RGB_OK, MKR_RGB_ERR_PARAM or MKR_RGB_ERR_TIMEOUT. */
uint8_t mkr_rgb_show(struct mkr_rgb_instance *this_rgb);

/* Waits until the current transfer is finished. Returns MKR_RGB_OK or MKR_RGB_ERR_TIMEOUT. */
uint8_t mkr_rgb_wait_ready(struct mkr_rgb_instance *this_rgb);

/* Helper: hue 0..1535 (6 x 256 steps around the colour wheel), full saturation and value. */
mkr_rgb_rgb_t mkr_rgb_hue(uint16_t hue);

#endif /* MKR_RGB_SPI_H */
