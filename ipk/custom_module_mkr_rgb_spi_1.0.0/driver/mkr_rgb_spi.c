#include "mkr_rgb_spi.h"
#include "reg_access.h"
#include <stddef.h>

#define MKR_RGB_DEFAULT_BRIGHTNESS 8u

uint8_t mkr_rgb_init(struct mkr_rgb_instance *this_rgb, uint32_t base_addr)
{
    uint32_t v = 0;

    if (NULL == this_rgb) {
        return MKR_RGB_ERR_PARAM;
    }
    this_rgb->base_address = base_addr;
    this_rgb->brightness   = MKR_RGB_DEFAULT_BRIGHTNESS;
    this_rgb->orientation  = 0u;
    mkr_rgb_clear(this_rgb);

    reg_32b_read(base_addr + MKR_RGB_SPI_REG_ID, &v);
    if (MKR_RGB_SPI_ID_VALUE != v) {
        return MKR_RGB_ERR_ID;
    }
    reg_32b_read(base_addr + MKR_RGB_SPI_REG_NLEDS, &v);
    if (v < MKR_RGB_NUM_LEDS) {
        return MKR_RGB_ERR_SIZE;
    }
    return MKR_RGB_OK;
}

uint8_t mkr_rgb_is_ready(struct mkr_rgb_instance *this_rgb)
{
    uint32_t v = 0;

    if (NULL == this_rgb) {
        return 0u;
    }
    reg_32b_read(this_rgb->base_address + MKR_RGB_SPI_REG_CTRL, &v);
    return (v & MKR_RGB_SPI_CTRL_READY) ? 1u : 0u;
}

uint8_t mkr_rgb_wait_ready(struct mkr_rgb_instance *this_rgb)
{
    uint32_t n;

    if (NULL == this_rgb) {
        return MKR_RGB_ERR_PARAM;
    }
    for (n = 0; n < MKR_RGB_TIMEOUT_POLLS; n++) {
        if (mkr_rgb_is_ready(this_rgb)) {
            return MKR_RGB_OK;
        }
    }
    return MKR_RGB_ERR_TIMEOUT;
}

void mkr_rgb_set_brightness(struct mkr_rgb_instance *this_rgb, uint8_t brightness)
{
    if (NULL != this_rgb) {
        this_rgb->brightness = (brightness > MKR_RGB_BRIGHTNESS_MAX) ? MKR_RGB_BRIGHTNESS_MAX : brightness;
    }
}

void mkr_rgb_set_orientation(struct mkr_rgb_instance *this_rgb, uint8_t flags)
{
    if (NULL != this_rgb) {
        this_rgb->orientation = flags & (MKR_RGB_FLIP_X | MKR_RGB_FLIP_Y);
    }
}

void mkr_rgb_fill(struct mkr_rgb_instance *this_rgb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t x, y;

    if (NULL == this_rgb) {
        return;
    }
    for (y = 0; y < MKR_RGB_HEIGHT; y++) {
        for (x = 0; x < MKR_RGB_WIDTH; x++) {
            this_rgb->fb[y][x].r = r;
            this_rgb->fb[y][x].g = g;
            this_rgb->fb[y][x].b = b;
        }
    }
}

void mkr_rgb_clear(struct mkr_rgb_instance *this_rgb)
{
    mkr_rgb_fill(this_rgb, 0u, 0u, 0u);
}

void mkr_rgb_set_pixel(struct mkr_rgb_instance *this_rgb, uint8_t x, uint8_t y,
                      uint8_t r, uint8_t g, uint8_t b)
{
    if ((NULL == this_rgb) || (x >= MKR_RGB_WIDTH) || (y >= MKR_RGB_HEIGHT)) {
        return;
    }
    this_rgb->fb[y][x].r = r;
    this_rgb->fb[y][x].g = g;
    this_rgb->fb[y][x].b = b;
}

void mkr_rgb_write_image(struct mkr_rgb_instance *this_rgb,
                        const mkr_rgb_rgb_t img[MKR_RGB_HEIGHT][MKR_RGB_WIDTH])
{
    uint32_t x, y;

    if ((NULL == this_rgb) || (NULL == img)) {
        return;
    }
    for (y = 0; y < MKR_RGB_HEIGHT; y++) {
        for (x = 0; x < MKR_RGB_WIDTH; x++) {
            this_rgb->fb[y][x] = img[y][x];
        }
    }
}

uint8_t mkr_rgb_show(struct mkr_rgb_instance *this_rgb)
{
    uint32_t base, x, y, sx, sy, idx;
    const mkr_rgb_rgb_t *p;

    if (NULL == this_rgb) {
        return MKR_RGB_ERR_PARAM;
    }
    if (MKR_RGB_OK != mkr_rgb_wait_ready(this_rgb)) {
        return MKR_RGB_ERR_TIMEOUT;
    }
    base = this_rgb->base_address;
    for (y = 0; y < MKR_RGB_HEIGHT; y++) {
        for (x = 0; x < MKR_RGB_WIDTH; x++) {
            /* physical position of the LED with chain index y * WIDTH + x */
            sx = (this_rgb->orientation & MKR_RGB_FLIP_X) ? (MKR_RGB_WIDTH - 1u - x) : x;
            sy = (this_rgb->orientation & MKR_RGB_FLIP_Y) ? (MKR_RGB_HEIGHT - 1u - y) : y;
            p   = &this_rgb->fb[sy][sx];
            idx = y * MKR_RGB_WIDTH + x;
            reg_32b_write(base + MKR_RGB_SPI_REG_PIX(idx),
                          MKR_RGB_APA102_WORD(this_rgb->brightness, p->r, p->g, p->b));
        }
    }
    reg_32b_write(base + MKR_RGB_SPI_REG_CTRL, MKR_RGB_SPI_CTRL_START);
    return MKR_RGB_OK;
}

mkr_rgb_rgb_t mkr_rgb_hue(uint16_t hue)
{
    mkr_rgb_rgb_t c = {0u, 0u, 0u};
    uint8_t f;

    hue = (uint16_t)(hue % 1536u);
    f   = (uint8_t)(hue & 0xFFu);
    switch (hue >> 8) {
    case 0:  c.r = 255u;              c.g = f;                  c.b = 0u;                 break; /* red -> yellow */
    case 1:  c.r = (uint8_t)(255u - f); c.g = 255u;             c.b = 0u;                 break; /* yellow -> green */
    case 2:  c.r = 0u;                c.g = 255u;               c.b = f;                  break; /* green -> cyan */
    case 3:  c.r = 0u;                c.g = (uint8_t)(255u - f); c.b = 255u;              break; /* cyan -> blue */
    case 4:  c.r = f;                 c.g = 0u;                 c.b = 255u;               break; /* blue -> magenta */
    default: c.r = 255u;              c.g = 0u;                 c.b = (uint8_t)(255u - f); break; /* magenta -> red */
    }
    return c;
}
