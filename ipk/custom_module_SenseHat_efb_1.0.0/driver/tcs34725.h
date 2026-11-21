/* TCS34725 RGB colour sensor (ams, Sense HAT V2, I2C address 0x39), BSP driver.
 *
 * Built on the SenseHat_efb I2C calls. Integer maths only. The part (TCS34721 / TCS34725) answers at
 * 0x39; the TCS34723 / TCS34727 variants answer at 0x29. Every register access uses the command
 * byte (0x80 | type | register); multi-byte reads use the auto-increment type (0xA0). */
#ifndef TCS34725_H
#define TCS34725_H

#include <stdint.h>
#include "SenseHat_efb.h"

#define TCS34725_ADDR         0x39u
#define TCS34725_ID_34725     0x44u   /* TCS34721 / TCS34725 */
#define TCS34725_ID_34727     0x4Du   /* TCS34723 / TCS34727 */

#define TCS34725_ERR_ID         (-40)   /* ID register mismatch: not a TCS3472x */
#define TCS34725_ERR_NOT_READY  (-41)   /* no completed integration cycle yet (AVALID = 0) */

/* Gain (CONTROL.AGAIN) */
#define TCS34725_GAIN_1X      0x00u
#define TCS34725_GAIN_4X      0x01u
#define TCS34725_GAIN_16X     0x02u
#define TCS34725_GAIN_60X     0x03u

struct tcs34725_data {
    uint16_t clear, red, green, blue;   /* raw 16-bit channel counts */
    uint8_t  r8, g8, b8;                /* colour normalised to the clear channel, 0..255 */
    int32_t  lux;                       /* approximate illuminance (DN40 formula, 1x/4x/16x/60x not compensated) */
};

#define TCS34725_ID_UNKNOWN     1       /* init succeeded but the ID register holds another value (see tcs34725_id) */

/* ID byte read by the last tcs34725_init (0x12 register). */
uint8_t tcs34725_id(void);

/* Checks the ID register (0x12; 0x00 / 0xFF fail, an unexpected value is reported as TCS34725_ID_UNKNOWN
 * and the sensor is configured anyway), sets the integration time (atime: 256 - ms / 2.4, for example
 * 0xEB = 50.4 ms, 0xD5 = 101 ms, 0xC0 = 154 ms) and the gain, then powers the sensor up and starts
 * the RGBC conversion (waits the 2.4 ms oscillator start-up). */
int tcs34725_init(struct sensehat_efb_instance *inst, uint8_t atime, uint8_t gain);
/* Reads the last completed RGBC sample. TCS34725_ERR_NOT_READY until one integration has finished. */
int tcs34725_read(struct sensehat_efb_instance *inst, struct tcs34725_data *out);
/* Powers the sensor down (ENABLE = 0). */
int tcs34725_power_down(struct sensehat_efb_instance *inst);

#endif /* TCS34725_H */
