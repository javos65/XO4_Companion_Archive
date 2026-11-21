/* LPS25H pressure / temperature sensor, BSP driver (see lps25h.h). */
#include <stddef.h>
#include "lps25h.h"

#define REG_WHO_AM_I   0x0Fu
#define REG_CTRL_REG1  0x20u
#define REG_STATUS     0x27u
#define REG_PRESS_OUT  0x28u    /* XL, L, H: 24-bit two's complement, 4096 LSB per hPa */
#define REG_TEMP_OUT   0x2Bu    /* L, H: 16-bit two's complement, 480 LSB per C, 42.5 C at 0 */
#define AUTO_INC       0x80u    /* MSB of the sub-address: auto-increment */

#define CTRL1_PD       0x80u
#define CTRL1_ODR_1HZ  0x10u    /* ODR = 001 */
#define CTRL1_BDU      0x04u
#define STATUS_T_DA    0x01u
#define STATUS_P_DA    0x02u

static int rd(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t *buf, uint32_t n)
{
    uint8_t r = (uint8_t)(reg | (n > 1u ? AUTO_INC : 0u));
    return sensehat_i2c_write_read(inst, LPS25H_ADDR, &r, 1u, buf, n);
}

int lps25h_init(struct sensehat_efb_instance *inst)
{
    uint8_t id = 0, ctrl = (uint8_t)(CTRL1_PD | CTRL1_ODR_1HZ | CTRL1_BDU);
    int rc = rd(inst, REG_WHO_AM_I, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (id != LPS25H_WHO_AM_I_VAL)
        return LPS25H_ERR_ID;
    return sensehat_i2c_write_block(inst, LPS25H_ADDR, REG_CTRL_REG1, &ctrl, 1u);
}

int lps25h_read(struct sensehat_efb_instance *inst, struct lps25h_data *out)
{
    uint8_t st = 0, b[5];
    int32_t p, t;
    int rc;

    if (out == NULL)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, REG_STATUS, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if ((st & (STATUS_T_DA | STATUS_P_DA)) != (STATUS_T_DA | STATUS_P_DA))
        return LPS25H_ERR_NOT_READY;

    rc = rd(inst, REG_PRESS_OUT, b, 3u);
    if (rc != SENSEHAT_OK)
        return rc;
    rc = rd(inst, REG_TEMP_OUT, &b[3], 2u);
    if (rc != SENSEHAT_OK)
        return rc;

    p = (int32_t)(((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0]);
    if (p & 0x800000)
        p -= 0x1000000;                                      /* sign extend 24 bit */
    t = (int16_t)(uint16_t)(((uint16_t)b[4] << 8) | b[3]);

    out->pressure_x100 = (p * 25) / 1024;                    /* p / 4096 hPa, x100 */
    out->temp_x100     = 4250 + (t * 5) / 24;                /* 42.5 + t / 480 C, x100 */
    return SENSEHAT_OK;
}

int lps25h_power_down(struct sensehat_efb_instance *inst)
{
    uint8_t ctrl = 0u;
    return sensehat_i2c_write_block(inst, LPS25H_ADDR, REG_CTRL_REG1, &ctrl, 1u);
}
