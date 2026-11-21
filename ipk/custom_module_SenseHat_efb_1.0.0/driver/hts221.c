/* HTS221 humidity / temperature sensor, BSP driver (see hts221.h). */
#include <stddef.h>
#include "hts221.h"

#define REG_WHO_AM_I   0x0Fu
#define REG_CTRL_REG1  0x20u
#define REG_STATUS     0x27u
#define REG_HUM_OUT    0x28u    /* H_OUT L,H then T_OUT L,H (16-bit two's complement) */
#define REG_CAL        0x30u    /* 16 calibration bytes, 0x30..0x3F */
#define AUTO_INC       0x80u

#define CTRL1_PD       0x80u
#define CTRL1_BDU      0x04u
#define CTRL1_ODR_1HZ  0x01u
#define STATUS_T_DA    0x01u
#define STATUS_H_DA    0x02u

static int rd(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t *buf, uint32_t n)
{
    uint8_t r = (uint8_t)(reg | (n > 1u ? AUTO_INC : 0u));
    return sensehat_i2c_write_read(inst, HTS221_ADDR, &r, 1u, buf, n);
}

static int16_t s16(const uint8_t *p)
{
    return (int16_t)(uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

int hts221_init(struct sensehat_efb_instance *inst, struct hts221_instance *dev)
{
    uint8_t id = 0, c[16], ctrl = (uint8_t)(CTRL1_PD | CTRL1_BDU | CTRL1_ODR_1HZ);
    int rc;

    if (dev == NULL)
        return SENSEHAT_ERR_ARG;
    dev->ready = 0u;
    rc = rd(inst, REG_WHO_AM_I, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (id != HTS221_WHO_AM_I_VAL)
        return HTS221_ERR_ID;
    rc = rd(inst, REG_CAL, c, 16u);
    if (rc != SENSEHAT_OK)
        return rc;

    /* c[0]=H0_rH_x2, c[1]=H1_rH_x2, c[2]=T0_degC_x8, c[3]=T1_degC_x8, c[5]=T1/T0 msb,
     * c[6..7]=H0_T0_OUT, c[10..11]=H1_T0_OUT, c[12..13]=T0_OUT, c[14..15]=T1_OUT */
    dev->h0_x2  = c[0];
    dev->h1_x2  = c[1];
    dev->t0_x8  = (int16_t)(((uint16_t)(c[5] & 0x03u) << 8) | c[2]);
    dev->t1_x8  = (int16_t)(((uint16_t)(c[5] & 0x0Cu) << 6) | c[3]);
    dev->h0_out = s16(&c[6]);
    dev->h1_out = s16(&c[10]);
    dev->t0_out = s16(&c[12]);
    dev->t1_out = s16(&c[14]);
    if (dev->h1_out == dev->h0_out || dev->t1_out == dev->t0_out)
        return HTS221_ERR_CAL;

    rc = sensehat_i2c_write_block(inst, HTS221_ADDR, REG_CTRL_REG1, &ctrl, 1u);
    if (rc == SENSEHAT_OK)
        dev->ready = 1u;
    return rc;
}

int hts221_read(struct sensehat_efb_instance *inst, const struct hts221_instance *dev,
                struct hts221_data *out)
{
    uint8_t st = 0, b[4];
    int32_t h, t, rh, tc;
    int rc;

    if (dev == NULL || out == NULL || !dev->ready)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, REG_STATUS, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if ((st & (STATUS_T_DA | STATUS_H_DA)) != (STATUS_T_DA | STATUS_H_DA))
        return HTS221_ERR_NOT_READY;
    rc = rd(inst, REG_HUM_OUT, b, 4u);
    if (rc != SENSEHAT_OK)
        return rc;

    h = s16(&b[0]);
    t = s16(&b[2]);

    /* humidity: H0 + (h - H0_out) * (H1 - H0) / (H1_out - H0_out), in %RH x 100 (H_x2 x 50) */
    rh = (int32_t)dev->h0_x2 * 50
       + ((h - dev->h0_out) * (((int32_t)dev->h1_x2 - dev->h0_x2) * 50)) / ((int32_t)dev->h1_out - dev->h0_out);
    if (rh < 0)     rh = 0;
    if (rh > 10000) rh = 10000;

    /* temperature: T0 + (t - T0_out) * (T1 - T0) / (T1_out - T0_out), calibration points in C x 8 */
    tc = ((int32_t)dev->t0_x8 * 100
       + ((t - dev->t0_out) * (((int32_t)dev->t1_x8 - dev->t0_x8) * 100)) / ((int32_t)dev->t1_out - dev->t0_out)) / 8;

    out->humidity_x100 = rh;
    out->temp_x100     = tc;
    return SENSEHAT_OK;
}

int hts221_power_down(struct sensehat_efb_instance *inst)
{
    uint8_t ctrl = 0u;
    return sensehat_i2c_write_block(inst, HTS221_ADDR, REG_CTRL_REG1, &ctrl, 1u);
}
