#include "fxls8971.h"
#include <stddef.h>

#define FXLS8971_MODE_POLLS  2000u   /* SYS_MODE polls (each one an I2C read, ~0.3 ms at 400 kHz) */

int fxls8971_read_reg(struct fxls8971_instance *dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_read_reg(dev->i2c, dev->addr, reg, val, 1u);
}

int fxls8971_write_reg(struct fxls8971_instance *dev, uint8_t reg, uint8_t val)
{
    return i2c_master_write_reg(dev->i2c, dev->addr, reg, &val, 1u);
}

int fxls8971_set_active(struct fxls8971_instance *dev, uint8_t active)
{
    uint8_t cfg1 = 0, mode = 0;
    uint32_t n;
    int ret;

    ret = fxls8971_read_reg(dev, FXLS8971_SENS_CONFIG1, &cfg1);
    if (ret) return ret;
    if (active) cfg1 |= FXLS8971_CFG1_ACTIVE;
    else        cfg1 &= (uint8_t)~FXLS8971_CFG1_ACTIVE;
    ret = fxls8971_write_reg(dev, FXLS8971_SENS_CONFIG1, cfg1);
    if (ret) return ret;

    for (n = 0; n < FXLS8971_MODE_POLLS; n++) {
        ret = fxls8971_read_reg(dev, FXLS8971_SYS_MODE, &mode);
        if (ret) return ret;
        mode &= 0x03u;
        if (active ? (mode != FXLS8971_MODE_STANDBY) : (mode == FXLS8971_MODE_STANDBY)) {
            return FXLS8971_OK;
        }
    }
    return FXLS8971_ERR_TIMEOUT;
}

int fxls8971_init(struct fxls8971_instance *dev, struct i2c_master_instance *i2c,
                  uint8_t addr, enum fxls8971_range range, enum fxls8971_odr odr)
{
    uint8_t id = 0, cfg1 = 0, cfg2 = 0, cfg3 = 0;
    int ret;

    if ((NULL == dev) || (NULL == i2c)) {
        return I2C_MASTER_ERR_PARAM;
    }
    dev->i2c   = i2c;
    dev->addr  = addr;
    dev->range = (uint8_t)range;
    dev->odr   = (uint8_t)odr;

    ret = fxls8971_read_reg(dev, FXLS8971_WHO_AM_I, &id);
    if (ret) return ret;
    if (FXLS8971_WHO_AM_I_VALUE != id) {
        return FXLS8971_ERR_ID;
    }
    ret = fxls8971_read_reg(dev, FXLS8971_PROD_REV, &dev->prod_rev);
    if (ret) return ret;

    /* configuration registers may only be written in Standby */
    ret = fxls8971_set_active(dev, 0u);
    if (ret) return ret;

    /* SENS_CONFIG2: WAKE_PM = SLEEP_PM = 00 (low-power mode), little endian, no fast read */
    ret = fxls8971_read_reg(dev, FXLS8971_SENS_CONFIG2, &cfg2);
    if (ret) return ret;
    cfg2 &= 0x02u;                                   /* keep AINC_TEMP only */
    ret = fxls8971_write_reg(dev, FXLS8971_SENS_CONFIG2, cfg2);
    if (ret) return ret;

    /* SENS_CONFIG3: WAKE_ODR = odr, SLEEP_ODR = odr */
    cfg3 = (uint8_t)(((uint8_t)odr << 4) | (uint8_t)odr);
    ret = fxls8971_write_reg(dev, FXLS8971_SENS_CONFIG3, cfg3);
    if (ret) return ret;

    /* SENS_CONFIG1: full-scale range, self-test off, ACTIVE still 0 */
    ret = fxls8971_read_reg(dev, FXLS8971_SENS_CONFIG1, &cfg1);
    if (ret) return ret;
    cfg1 &= (uint8_t)~(FXLS8971_CFG1_FSR_MASK | 0x70u | FXLS8971_CFG1_ACTIVE);
    cfg1 |= (uint8_t)(((uint8_t)range << 1) & FXLS8971_CFG1_FSR_MASK);
    ret = fxls8971_write_reg(dev, FXLS8971_SENS_CONFIG1, cfg1);
    if (ret) return ret;

    return fxls8971_set_active(dev, 1u);
}

int fxls8971_data_ready(struct fxls8971_instance *dev)
{
    uint8_t st = 0;
    int ret = fxls8971_read_reg(dev, FXLS8971_INT_STATUS, &st);

    if (ret) return -ret;
    return (st & FXLS8971_SRC_DRDY) ? 1 : 0;
}

/* 12-bit two's complement, little endian, right justified (SENS_CONFIG2.LE_BE = 0) */
static int16_t fxls8971_s12(uint8_t lsb, uint8_t msb)
{
    int16_t v = (int16_t)(((uint16_t)(msb & 0x0Fu) << 8) | lsb);
    if (v & 0x0800) {
        v = (int16_t)(v - 0x1000);
    }
    return v;
}

int fxls8971_read_xyz(struct fxls8971_instance *dev, struct fxls8971_data *data)
{
    uint8_t b[6];
    int ret;

    if (NULL == data) {
        return I2C_MASTER_ERR_PARAM;
    }
    ret = i2c_master_read_reg(dev->i2c, dev->addr, FXLS8971_OUT_X_LSB, b, sizeof(b));
    if (ret) return ret;
    data->x = fxls8971_s12(b[0], b[1]);
    data->y = fxls8971_s12(b[2], b[3]);
    data->z = fxls8971_s12(b[4], b[5]);
    return FXLS8971_OK;
}

int fxls8971_read_temp(struct fxls8971_instance *dev, int8_t *deg_c)
{
    uint8_t t = 0;
    int ret = fxls8971_read_reg(dev, FXLS8971_TEMP_OUT, &t);

    if (ret) return ret;
    *deg_c = (int8_t)((int8_t)t + 25);
    return FXLS8971_OK;
}

int32_t fxls8971_to_mg(const struct fxls8971_instance *dev, int16_t raw)
{
    /* 1024 LSB/g at +/-2 g, halving for every range step: mg = raw * 1000 * 2^range / 1024 */
    return ((int32_t)raw * 1000 * (1 << dev->range)) / 1024;
}
