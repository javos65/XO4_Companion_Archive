/* HTS221 humidity / temperature sensor (Sense HAT, I2C address 0x5F), BSP driver.
 *
 * Built on the SenseHat_efb I2C calls. Integer maths only. Values are in hundredths:
 * humidity_x100 = 4512 means 45.12 %RH, temp_x100 = 2310 means 23.10 C.
 * The factory calibration is read once in hts221_init and kept in the instance. */
#ifndef HTS221_H
#define HTS221_H

#include <stdint.h>
#include "SenseHat_efb.h"

#define HTS221_ADDR           0x5Fu
#define HTS221_WHO_AM_I_VAL   0xBCu

#define HTS221_ERR_ID         (-20)   /* WHO_AM_I mismatch: not an HTS221 */
#define HTS221_ERR_NOT_READY  (-21)   /* no new sample yet (the first one takes up to 1 s after init) */
#define HTS221_ERR_CAL        (-22)   /* calibration data unusable (equal calibration points) */

struct hts221_instance {
    int16_t  h0_out, h1_out, t0_out, t1_out;   /* raw calibration points */
    int16_t  h0_x2, h1_x2;                     /* calibration humidity points, %RH x 2 */
    int16_t  t0_x8, t1_x8;                     /* calibration temperature points, C x 8 */
    uint8_t  ready;                            /* set when hts221_init has succeeded */
};

struct hts221_data {
    int32_t humidity_x100;   /* %RH x 100, limited to 0..10000 */
    int32_t temp_x100;       /* degrees C x 100 */
};

/* Checks WHO_AM_I (0x0F = 0xBC), reads the calibration (0x30..0x3F) and powers the sensor up:
 * 1 Hz output rate, block data update. */
int hts221_init(struct sensehat_efb_instance *inst, struct hts221_instance *dev);
/* Reads the latest humidity and temperature and applies the linear calibration. */
int hts221_read(struct sensehat_efb_instance *inst, const struct hts221_instance *dev,
                struct hts221_data *out);
/* Powers the sensor down. */
int hts221_power_down(struct sensehat_efb_instance *inst);

#endif /* HTS221_H */
