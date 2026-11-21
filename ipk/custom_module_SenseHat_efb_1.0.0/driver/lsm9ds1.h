/* LSM9DS1 9-axis IMU on the Sense HAT, BSP driver.
 *
 * Two I2C devices share the part: the accelerometer / gyroscope / temperature block at 0x6A and
 * the magnetometer at 0x1C. Built on the SenseHat_efb I2C calls, integer maths only.
 *
 * Units: acceleration in mg (+-2 g range), angular rate in mdps (+-245 dps range), magnetic field
 * in mgauss (+-4 gauss range), temperature in hundredths of a degree C. */
#ifndef LSM9DS1_H
#define LSM9DS1_H

#include <stdint.h>
#include "SenseHat_efb.h"

#define LSM9DS1_AG_ADDR        0x6Au
#define LSM9DS1_MAG_ADDR       0x1Cu
#define LSM9DS1_AG_WHO_AM_I    0x68u
#define LSM9DS1_MAG_WHO_AM_I   0x3Du

#define LSM9DS1_ERR_ID         (-30)   /* WHO_AM_I mismatch */
#define LSM9DS1_ERR_NOT_READY  (-31)   /* no new sample yet */

struct lsm9ds1_ag_data {
    int32_t accel_mg[3];     /* x, y, z in mg */
    int32_t gyro_mdps[3];    /* x, y, z in milli-degrees per second */
    int32_t temp_x100;       /* degrees C x 100 (about 25 C at raw 0) */
};

struct lsm9ds1_mag_data {
    int32_t field_mgauss[3]; /* x, y, z in milligauss */
};

/* Accelerometer / gyroscope: WHO_AM_I 0x68, 119 Hz, +-2 g, +-245 dps, block data update. */
int lsm9ds1_ag_init(struct sensehat_efb_instance *inst);
int lsm9ds1_ag_read(struct sensehat_efb_instance *inst, struct lsm9ds1_ag_data *out);

/* Magnetometer: WHO_AM_I 0x3D, 80 Hz high performance, +-4 gauss, continuous conversion. */
int lsm9ds1_mag_init(struct sensehat_efb_instance *inst);
int lsm9ds1_mag_read(struct sensehat_efb_instance *inst, struct lsm9ds1_mag_data *out);

/* Power both blocks down. */
int lsm9ds1_power_down(struct sensehat_efb_instance *inst);

#endif /* LSM9DS1_H */
