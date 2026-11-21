/* Register map of the MKR_RGB_SPI block (custom:module:mkr_rgb_spi:1.0.0). */
#ifndef MKR_RGB_SPI_REGS_H
#define MKR_RGB_SPI_REGS_H

#include <stdint.h>

/* byte offsets from the base address */
#define MKR_RGB_SPI_REG_ID          0x000u   /* R: MKR_RGB_SPI_ID_VALUE */
#define MKR_RGB_SPI_REG_CTRL        0x004u   /* bit0 START (W), bit1 READY (R) */
#define MKR_RGB_SPI_REG_NLEDS       0x008u   /* R: number of LEDs (NUM_LEDS parameter) */
#define MKR_RGB_SPI_REG_PIX(i)      (0x00Cu + 4u * (uint32_t)(i))   /* LED frame words, chain order */

#define MKR_RGB_SPI_CTRL_START      (1u << 0)
#define MKR_RGB_SPI_CTRL_READY      (1u << 1)

#define MKR_RGB_SPI_ID_VALUE        0x52474231u   /* "RGB1" */

/* APA102 LED frame: 111 | brightness[4:0] | blue | green | red */
#define MKR_RGB_APA102_WORD(br, r, g, b) \
    (0xE0000000u | (((uint32_t)(br) & 0x1Fu) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

#endif /* MKR_RGB_SPI_REGS_H */
