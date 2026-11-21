/* LPS25H pressure / temperature sensor (Sense HAT, I2C address 0x5C), BSP driver.
 *
 * Built on the SenseHat_efb I2C calls. Integer maths only. Pressure and temperature are returned
 * in hundredths of a unit: pressure_x100 = 101325 means 1013.25 hPa, temp_x100 = 2831 means 28.31 C.
 * Return values: 0 (SENSEHAT_OK), the I2C codes of SenseHat_efb.h, or an LPS25H_ERR_* code. */
#ifndef LPS25H_H
#define LPS25H_H

#include <stdint.h>
#include "SenseHat_efb.h"

#define LPS25H_ADDR           0x5Cu
#define LPS25H_WHO_AM_I_VAL   0xBDu

#define LPS25H_ERR_ID         (-10)   /* WHO_AM_I mismatch: not an LPS25H */
#define LPS25H_ERR_NOT_READY  (-11)   /* no new sample yet (the first one takes up to 1 s after init) */

struct lps25h_data {
    int32_t pressure_x100;   /* hPa x 100 */
    int32_t temp_x100;       /* degrees C x 100 */
};

/* Checks WHO_AM_I (0x0F = 0xBD), then powers the sensor up: 1 Hz output rate, block data update. */
int lps25h_init(struct sensehat_efb_instance *inst);
/* Reads the latest pressure and temperature. LPS25H_ERR_NOT_READY when a new sample is not there. */
int lps25h_read(struct sensehat_efb_instance *inst, struct lps25h_data *out);
/* Powers the sensor down. */
int lps25h_power_down(struct sensehat_efb_instance *inst);

#endif /* LPS25H_H */
