/* SenseHat_efb 1.0.0: BSP driver for the Raspberry Pi Sense HAT (8x8 RGB LED matrix and 5-way
 * joystick, ATTINY88-MUR at I2C address 0x46) on the MachXO4 EFB secondary I2C controller,
 * accessed through the SenseHat_efb APB block.
 *
 * Hardware: the lscc_efb IP (APB, 32-bit data, 10-bit address) behind SenseHat_efb, secondary I2C
 * enabled, i2c2_scl / i2c2_sda routed to the HAT header pins (SDA1 / SCL1), 3.3 V. The driver is
 * the only I2C master; it never uses interrupts. All functions are blocking and poll the
 * controller with a bounded loop. Every function returns SENSEHAT_OK (0) or a negative
 * SENSEHAT_ERR_* code; the set-up function returns 0 or 1 (the Propel driver convention).
 *
 * Arithmetic is integer only (RV32I, no multiplier assumed, no floating point).
 */
#ifndef SENSEHAT_EFB_H
#define SENSEHAT_EFB_H

#include <stdint.h>
#include "SenseHat_efb_regs.h"

/* ---- return codes ---- */
#define SENSEHAT_OK            0
#define SENSEHAT_ERR_TIMEOUT   (-1)   /* controller flag did not change within the poll limit */
#define SENSEHAT_ERR_NACK      (-2)   /* addressed device or data byte not acknowledged */
#define SENSEHAT_ERR_ARBL      (-3)   /* arbitration lost or bus busy before the transfer */
#define SENSEHAT_ERR_ARG       (-4)   /* null pointer, length zero or address out of range */

/* Default poll limit per wait (each poll is one APB read) */
#define SENSEHAT_POLL_LIMIT_DEFAULT  200000u

struct sensehat_efb_instance {
    uint32_t base_address;      /* SenseHat_efb APB base address */
    uint32_t poll_limit;        /* poll limit per wait */
    uint32_t wb_clk_hz;         /* EFB WISHBONE (APB) clock in Hz, set by sensehat_i2c_init */
    uint32_t i2c_hz;            /* I2C clock in Hz actually programmed */
    uint8_t  last_sr;           /* I2C_2_SR when the last error occurred */
    uint8_t  last_addr;         /* 7-bit address of the last transfer */
    void   (*delay_us)(uint32_t us);  /* optional microsecond delay (read real-time wait); NULL = loop */
};

/* ---- set-up ---- */
/* Binds the instance to the block base address. Does not access the hardware.
 * Returns 0, or 1 for a null pointer. */
unsigned char sensehat_efb_init(struct sensehat_efb_instance *inst, uint32_t base_addr);
/* Programs the EFB I2C2 prescale for i2c_hz from wb_clk_hz (f = wb_clk / (prescale x 4)), then
 * enables the controller (CR = I2CEN | SDA delay off, 0x8C). Returns SENSEHAT_OK or
 * SENSEHAT_ERR_ARG when the prescale is outside 1..1023. Example: 12 MHz and 100 kHz give 30. */
int sensehat_i2c_init(struct sensehat_efb_instance *inst, uint32_t wb_clk_hz, uint32_t i2c_hz);
/* Resets the I2C core (rewrites CR) and reprograms the enable. Use after a timeout. */
int sensehat_i2c_reset(struct sensehat_efb_instance *inst);
/* Best-effort bus recovery: issues a STOP and resets the core. The EFB controller cannot
 * generate free-running SCL pulses, so a stuck SDA held by a target can not be cleared here;
 * the function reports SENSEHAT_ERR_ARBL when BUSY stays set afterwards. */
int sensehat_i2c_recover(struct sensehat_efb_instance *inst);

/* ---- raw I2C transfers (7-bit address) ---- */
int sensehat_i2c_write(struct sensehat_efb_instance *inst, uint8_t addr, const uint8_t *data, uint32_t len);
/* Writes wlen bytes, repeated START, reads rlen bytes (last byte NACK + STOP). */
int sensehat_i2c_write_read(struct sensehat_efb_instance *inst, uint8_t addr,
                            const uint8_t *wdata, uint32_t wlen, uint8_t *rdata, uint32_t rlen);
/* One transaction: START, address, reg, len data bytes, STOP (no copy of the data). */
int sensehat_i2c_write_block(struct sensehat_efb_instance *inst, uint8_t addr, uint8_t reg,
                             const uint8_t *data, uint32_t len);

/* ---- Sense HAT ---- */
/* Checks that the ATTINY88 at 0x46 acknowledges and returns the joystick register. */
int sensehat_probe(struct sensehat_efb_instance *inst);
/* Writes the 192-byte framebuffer in one 193-byte transaction (register 0x00 + 192 pixel bytes).
 * Layout: fb[y * 24 + plane * 8 + x], plane 0 = red, 1 = green, 2 = blue. The bytes are sent
 * as they are (no masking): keep every value at or below SENSEHAT_COLOR_MAX, for example by
 * filling the buffer with sensehat_fb_set_pixel, which clamps. */
int sensehat_fb_write(struct sensehat_efb_instance *inst, const uint8_t fb[SENSEHAT_FB_BYTES]);
/* Framebuffer helpers (RAM only, no I2C) */
void sensehat_fb_clear(uint8_t fb[SENSEHAT_FB_BYTES]);
/* Sets one pixel (x, y = 0..7); each colour is clamped to SENSEHAT_COLOR_MAX. Out-of-range
 * coordinates are ignored. */
void sensehat_fb_set_pixel(uint8_t fb[SENSEHAT_FB_BYTES], uint8_t x, uint8_t y,
                           uint8_t r, uint8_t g, uint8_t b);
/* Reads the joystick register 0xF2 (bitmask SENSEHAT_JOY_*, bits 7..5 masked to 0). */
int sensehat_joystick_read(struct sensehat_efb_instance *inst, uint8_t *mask);

/* Raw read of one EFB register (byte offset, e.g. SENSEHAT_EFB_I2C_SR) for diagnostics. */
uint8_t sensehat_efb_reg(const struct sensehat_efb_instance *inst, uint32_t off);

const char *sensehat_strerror(int code);

#endif /* SENSEHAT_EFB_H */
