/* LSM9DS1 IMU, BSP driver (see lsm9ds1.h). */
#include <stddef.h>
#include "lsm9ds1.h"

/* common */
#define REG_WHO_AM_I        0x0Fu

/* accelerometer / gyroscope (0x6A) */
#define AG_CTRL_REG1_G      0x10u   /* ODR_G[7:5] FS_G[4:3] */
#define AG_OUT_TEMP_L       0x15u
#define AG_STATUS_REG       0x17u   /* bit0 XLDA, bit1 GDA, bit2 TDA */
#define AG_OUT_X_L_G        0x18u
#define AG_CTRL_REG6_XL     0x20u   /* ODR_XL[7:5] FS_XL[4:3] */
#define AG_CTRL_REG8        0x22u   /* bit6 BDU, bit2 IF_ADD_INC */
#define AG_OUT_X_L_XL       0x28u
#define AG_STATUS_XLDA      0x01u
#define AG_STATUS_GDA       0x02u
#define AG_STATUS_TDA       0x04u

/* magnetometer (0x1C) */
#define M_CTRL_REG1         0x20u   /* TEMP_COMP, OM[6:5], DO[4:2] */
#define M_CTRL_REG2         0x21u   /* FS[6:5] */
#define M_CTRL_REG3         0x22u   /* MD[1:0] = 00 continuous */
#define M_CTRL_REG4         0x23u   /* OMZ[3:2] */
#define M_CTRL_REG5         0x24u   /* bit6 BDU */
#define M_STATUS_REG        0x27u   /* bit3 ZYXDA */
#define M_OUT_X_L           0x28u
#define M_STATUS_ZYXDA      0x08u

static int rd(struct sensehat_efb_instance *inst, uint8_t addr, uint8_t reg, uint8_t *buf, uint32_t n)
{
    return sensehat_i2c_write_read(inst, addr, &reg, 1u, buf, n);
}

static int wr(struct sensehat_efb_instance *inst, uint8_t addr, uint8_t reg, uint8_t val)
{
    return sensehat_i2c_write_block(inst, addr, reg, &val, 1u);
}

static int32_t s16(const uint8_t *p)
{
    return (int16_t)(uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

int lsm9ds1_ag_init(struct sensehat_efb_instance *inst)
{
    uint8_t id = 0;
    int rc = rd(inst, LSM9DS1_AG_ADDR, REG_WHO_AM_I, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (id != LSM9DS1_AG_WHO_AM_I)
        return LSM9DS1_ERR_ID;
    if ((rc = wr(inst, LSM9DS1_AG_ADDR, AG_CTRL_REG8, 0x44u)) != SENSEHAT_OK)      /* BDU + auto-increment */
        return rc;
    if ((rc = wr(inst, LSM9DS1_AG_ADDR, AG_CTRL_REG1_G, 0x60u)) != SENSEHAT_OK)    /* 119 Hz, 245 dps */
        return rc;
    return wr(inst, LSM9DS1_AG_ADDR, AG_CTRL_REG6_XL, 0x60u);                      /* 119 Hz, +-2 g */
}

int lsm9ds1_ag_read(struct sensehat_efb_instance *inst, struct lsm9ds1_ag_data *out)
{
    uint8_t st = 0, g[6], a[6], t[2];
    int rc, i;

    if (out == NULL)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, LSM9DS1_AG_ADDR, AG_STATUS_REG, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if ((st & (AG_STATUS_XLDA | AG_STATUS_GDA | AG_STATUS_TDA)) != (AG_STATUS_XLDA | AG_STATUS_GDA | AG_STATUS_TDA))
        return LSM9DS1_ERR_NOT_READY;
    if ((rc = rd(inst, LSM9DS1_AG_ADDR, AG_OUT_X_L_G, g, 6u)) != SENSEHAT_OK) return rc;
    if ((rc = rd(inst, LSM9DS1_AG_ADDR, AG_OUT_X_L_XL, a, 6u)) != SENSEHAT_OK) return rc;
    if ((rc = rd(inst, LSM9DS1_AG_ADDR, AG_OUT_TEMP_L, t, 2u)) != SENSEHAT_OK) return rc;

    for (i = 0; i < 3; i++) {
        out->accel_mg[i]   = (s16(&a[2 * i]) * 61) / 1000;     /* 0.061 mg per LSB at +-2 g */
        out->gyro_mdps[i]  = (s16(&g[2 * i]) * 875) / 100;     /* 8.75 mdps per LSB at 245 dps */
    }
    out->temp_x100 = 2500 + (s16(t) * 25) / 4;                  /* 25 C + raw / 16 */
    return SENSEHAT_OK;
}

int lsm9ds1_mag_init(struct sensehat_efb_instance *inst)
{
    uint8_t id = 0;
    int rc = rd(inst, LSM9DS1_MAG_ADDR, REG_WHO_AM_I, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (id != LSM9DS1_MAG_WHO_AM_I)
        return LSM9DS1_ERR_ID;
    if ((rc = wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG1, 0x5Cu)) != SENSEHAT_OK) return rc;   /* high perf XY, 80 Hz */
    if ((rc = wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG2, 0x00u)) != SENSEHAT_OK) return rc;   /* +-4 gauss */
    if ((rc = wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG3, 0x00u)) != SENSEHAT_OK) return rc;   /* continuous */
    if ((rc = wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG4, 0x08u)) != SENSEHAT_OK) return rc;   /* high perf Z */
    return wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG5, 0x40u);                                  /* BDU */
}

int lsm9ds1_mag_read(struct sensehat_efb_instance *inst, struct lsm9ds1_mag_data *out)
{
    uint8_t st = 0, b[6];
    int rc, i;

    if (out == NULL)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, LSM9DS1_MAG_ADDR, M_STATUS_REG, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (!(st & M_STATUS_ZYXDA))
        return LSM9DS1_ERR_NOT_READY;
    /* One register per transfer: independent of how the magnetometer handles auto-increment. */
    for (i = 0; i < 6; i++) {
        rc = rd(inst, LSM9DS1_MAG_ADDR, (uint8_t)(M_OUT_X_L + i), &b[i], 1u);
        if (rc != SENSEHAT_OK)
            return rc;
    }
    for (i = 0; i < 3; i++)
        out->field_mgauss[i] = (s16(&b[2 * i]) * 14) / 100;     /* 0.14 mgauss per LSB at +-4 gauss */
    return SENSEHAT_OK;
}

int lsm9ds1_power_down(struct sensehat_efb_instance *inst)
{
    int rc = wr(inst, LSM9DS1_AG_ADDR, AG_CTRL_REG1_G, 0x00u);
    if (rc == SENSEHAT_OK)
        rc = wr(inst, LSM9DS1_AG_ADDR, AG_CTRL_REG6_XL, 0x00u);
    if (rc == SENSEHAT_OK)
        rc = wr(inst, LSM9DS1_MAG_ADDR, M_CTRL_REG3, 0x03u);   /* power-down mode */
    return rc;
}
