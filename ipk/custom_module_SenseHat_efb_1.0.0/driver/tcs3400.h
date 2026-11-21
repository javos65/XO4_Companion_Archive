/* TCS3400 colour light sensor (ams OSRAM, TCS34001 / 34003 / 34005 / 34007; Sense HAT V2 uses the
 * ID 0x90 variant at I2C address 0x39), BSP driver. Register map: TCS3400 datasheet DS000411 v3-00.
 *
 * Built on the SenseHat_efb I2C calls. Integer maths only. Unlike the TCS3472x family this part has
 * no command register: every register is addressed directly (ENABLE 0x80 ... CDATAL 0x94). */
#ifndef TCS3400_H
#define TCS3400_H

#include <stdint.h>
#include "SenseHat_efb.h"

#define TCS3400_ADDR           0x39u
#define TCS3400_ID_MASK        0xFCu   /* ID[7:2] = 100100; ID[1:0] = VID */
#define TCS3400_ID_VALUE       0x90u   /* TCS34001 / TCS34005 (VID 00); VID 11 (0x93) = TCS34003 / TCS34007 */

#define TCS3400_ERR_ID         (-50)   /* ID register is not a TCS3400 (ID[7:2] != 100100) */
#define TCS3400_ERR_NOT_READY  (-51)   /* no completed RGBC cycle yet (AVALID = 0) */

/* Gain (CONTROL.AGAIN) */
#define TCS3400_GAIN_1X        0x00u
#define TCS3400_GAIN_4X        0x01u
#define TCS3400_GAIN_16X       0x02u
#define TCS3400_GAIN_64X       0x03u

/* Integration time: ATIME = 256 - cycles, one cycle = 2.78 ms, max count = min(cycles x 1024, 65535) */
#define TCS3400_ATIME_2MS8     0xFFu   /* 1 cycle, 2.78 ms */
#define TCS3400_ATIME_27MS8    0xF6u   /* 10 cycles */
#define TCS3400_ATIME_103MS    0xDBu   /* 37 cycles (default of this BSP) */
#define TCS3400_ATIME_178MS    0xC0u   /* 64 cycles, full 16-bit range */

struct tcs3400_data {
    uint16_t clear, red, green, blue;   /* raw 16-bit channel counts */
    uint8_t  r8, g8, b8;                /* colour normalised to the clear channel, 0..255 */
    uint8_t  saturated;                 /* 1 when STATUS.ASAT is set (reduce gain or integration time) */
};

/* Checks the ID register (0x92), sets integration time (atime) and gain, writes the reserved CONFIG
 * bit as required (0x40), powers up and starts the RGBC conversion (waits 3 ms between PON and AEN).
 * Returns SENSEHAT_OK, an I2C error or TCS3400_ERR_ID. */
int tcs3400_init(struct sensehat_efb_instance *inst, uint8_t atime, uint8_t gain);
/* ID byte read by the last tcs3400_init (register 0x92). */
uint8_t tcs3400_id(void);
/* Reads the last completed RGBC sample (TCS3400_ERR_NOT_READY until one cycle has finished). */
int tcs3400_read(struct sensehat_efb_instance *inst, struct tcs3400_data *out);
/* Powers the sensor down (ENABLE = 0). */
int tcs3400_power_down(struct sensehat_efb_instance *inst);

#endif /* TCS3400_H */
