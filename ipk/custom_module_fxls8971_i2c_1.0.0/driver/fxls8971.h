/* BSP driver for the NXP FXLS8971CF 3-axis accelerometer on I2C.
 *
 * Part of the FXLS8971_I2C IP (custom:module:fxls8971_i2c:1.0.0); uses its I2C layer (i2c_master.h). Register map and behaviour from
 * datasheet DS15141 Rev 1.
 *
 * Board note (MachXO4 Companion board): BT_MODE is pulled up to VDD, so the sensor
 * always boots in motion detection mode and goes straight to WAKE with a 0.78 Hz
 * output data rate (WAKE_ODR = 0xC). fxls8971_init() puts it into Standby, sets the
 * range and a low-power-mode ODR (the datasheet only advises LPM when BT_MODE = VDD)
 * and makes it Active again.
 */
#ifndef FXLS8971_H
#define FXLS8971_H

#include <stdint.h>
#include "i2c_master.h"

/* 7-bit I2C address: 0x18 with SA0 = 0, 0x19 with SA0 = 1 */
#define FXLS8971_ADDR_SA0_LOW    0x18u
#define FXLS8971_ADDR_SA0_HIGH   0x19u

/* registers */
#define FXLS8971_INT_STATUS      0x00u
#define FXLS8971_TEMP_OUT        0x01u
#define FXLS8971_VECM_LSB        0x02u
#define FXLS8971_OUT_X_LSB       0x04u
#define FXLS8971_PROD_REV        0x12u
#define FXLS8971_WHO_AM_I        0x13u
#define FXLS8971_SYS_MODE        0x14u
#define FXLS8971_SENS_CONFIG1    0x15u
#define FXLS8971_SENS_CONFIG2    0x16u
#define FXLS8971_SENS_CONFIG3    0x17u
#define FXLS8971_SENS_CONFIG4    0x18u
#define FXLS8971_SENS_CONFIG5    0x19u

#define FXLS8971_WHO_AM_I_VALUE  0x83u

/* INT_STATUS */
#define FXLS8971_SRC_DRDY        0x80u
#define FXLS8971_SRC_BOOT        0x01u

/* SENS_CONFIG1 */
#define FXLS8971_CFG1_RST        0x80u
#define FXLS8971_CFG1_FSR_MASK   0x06u
#define FXLS8971_CFG1_ACTIVE     0x01u

/* SYS_MODE[1:0] */
#define FXLS8971_MODE_STANDBY    0u
#define FXLS8971_MODE_WAKE       1u
#define FXLS8971_MODE_SLEEP      2u
#define FXLS8971_MODE_EXT_TRIG   3u

/* full-scale range, SENS_CONFIG1[2:1] */
enum fxls8971_range {
    FXLS8971_RANGE_2G  = 0,   /* 0.98 mg/LSB */
    FXLS8971_RANGE_4G  = 1,   /* 1.95 mg/LSB */
    FXLS8971_RANGE_8G  = 2,   /* 3.91 mg/LSB */
    FXLS8971_RANGE_16G = 3    /* 7.81 mg/LSB */
};

/* low-power mode output data rate, SENS_CONFIG3 WAKE_ODR[3:0] */
enum fxls8971_odr {
    FXLS8971_ODR_3200HZ = 0x0, FXLS8971_ODR_1600HZ = 0x1, FXLS8971_ODR_800HZ = 0x2,
    FXLS8971_ODR_400HZ  = 0x3, FXLS8971_ODR_200HZ  = 0x4, FXLS8971_ODR_100HZ = 0x5,
    FXLS8971_ODR_50HZ   = 0x6, FXLS8971_ODR_25HZ   = 0x7, FXLS8971_ODR_12HZ5 = 0x8,
    FXLS8971_ODR_6HZ25  = 0x9, FXLS8971_ODR_3HZ125 = 0xA, FXLS8971_ODR_1HZ563 = 0xB,
    FXLS8971_ODR_0HZ781 = 0xC
};

/* driver return codes (I2C_MASTER_ERR_* codes are passed through) */
#define FXLS8971_OK              0
#define FXLS8971_ERR_ID          10   /* WHO_AM_I does not match */
#define FXLS8971_ERR_TIMEOUT     11   /* mode change did not happen */

struct fxls8971_instance {
    struct i2c_master_instance *i2c;
    uint8_t  addr;          /* 7-bit address */
    uint8_t  range;         /* enum fxls8971_range */
    uint8_t  odr;           /* enum fxls8971_odr */
    uint8_t  prod_rev;      /* PROD_REV read at init */
};

struct fxls8971_data {
    int16_t x, y, z;        /* raw 12-bit samples, sign extended */
};

/* Probes the sensor (WHO_AM_I), then configures range and ODR and makes it Active. */
int fxls8971_init(struct fxls8971_instance *dev, struct i2c_master_instance *i2c,
                  uint8_t addr, enum fxls8971_range range, enum fxls8971_odr odr);

int fxls8971_read_reg(struct fxls8971_instance *dev, uint8_t reg, uint8_t *val);
int fxls8971_write_reg(struct fxls8971_instance *dev, uint8_t reg, uint8_t val);

/* Standby (active = 0) or Active (active = 1). Waits until SYS_MODE follows. */
int fxls8971_set_active(struct fxls8971_instance *dev, uint8_t active);

/* Returns 1 when a new X/Y/Z sample is ready (INT_STATUS.SRC_DRDY), 0 when not, < 0 on error. */
int fxls8971_data_ready(struct fxls8971_instance *dev);

/* Reads X, Y and Z in one burst (registers 0x04..0x09). Clears SRC_DRDY. */
int fxls8971_read_xyz(struct fxls8971_instance *dev, struct fxls8971_data *data);

/* Reads the temperature: TEMP_OUT is a signed byte, 0 = 25 degC, 1 degC per LSB. */
int fxls8971_read_temp(struct fxls8971_instance *dev, int8_t *deg_c);

/* Converts a raw sample to milli-g for the configured range. */
int32_t fxls8971_to_mg(const struct fxls8971_instance *dev, int16_t raw);

#endif /* FXLS8971_H */
